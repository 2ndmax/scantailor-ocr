// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <CommandLineRun.h>
#include <DefaultParams.h>
#include <ImageFileInfo.h>
#include <ImageId.h>
#include <PageId.h>
#include <PageSelectionAccessor.h>
#include <ProcessingOptions.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectSession.h>
#include <StageSequence.h>
#include <filters/output/Settings.h>

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <optional>

#include "TestHelpers.h"

namespace {
/**
 * A project of one scanned page, saved in \p dir as the app saves a new one, with its output
 * going to \p outDir there. With \p dpi 0, the page lacks its resolution. Its PDF gets the text
 * recognition with \p ocrLanguages, none without; by default, a project would recognize German
 * and English, which needn't be installed here. \p formats are the ones ticked in the project.
 * Returns the project file, or an empty string if it couldn't be created.
 */
QString createProject(const QTemporaryDir& dir,
                      const int dpi,
                      const QStringList& ocrLanguages = QStringList(),
                      const QString& outDirName = QStringLiteral("out"),
                      const ProcessingOptions::Formats& formats = ProcessingOptions::Formats()) {
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath("page.png"), dpi)};
  if (files.front().imageInfo().empty()) {
    return QString();
  }
  const QString outDir = dir.filePath(outDirName);
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  const ProjectSession session = ProjectSession::createNew(ProjectPages::createNew(files, startValues), outDir,
                                                           QString(), startValues, PageSelectionAccessor(nullptr));
  ProcessingOptions& options = *session.stages->processingOptions();
  options.pdf.textLayer = !ocrLanguages.isEmpty();
  options.ocr.languages = ocrLanguages;
  options.formats = formats;
  const QString projectFile = dir.filePath("project.ScanTailor");
  return session.write(projectFile) ? projectFile : QString();
}

/** Runs the tool on \p projectFile; what it writes goes to \p out and \p err. */
CommandLineRun::ExitCode run(const QString& projectFile,
                             QString* out,
                             QString* err,
                             const std::optional<QString>& exportDir = std::nullopt,
                             const std::optional<QString>& formats = std::nullopt,
                             const std::optional<QString>& exportName = std::nullopt,
                             const std::atomic<bool>& cancel = std::atomic<bool>(false)) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.exportDir = exportDir;
  options.exportName = exportName;
  options.formats = formats;
  return CommandLineRun::run(options, outStream, errStream, cancel);
}

/** Exports into the folder of \p file under its name, with --export-folder and --export-name. */
CommandLineRun::ExitCode runExport(const QString& projectFile,
                                   QString* out,
                                   QString* err,
                                   const QString& file,
                                   const std::optional<QString>& formats = std::nullopt) {
  const QFileInfo info(file);
  return run(projectFile, out, err, info.path(), formats, info.fileName());
}

ProcessingOptions::Formats parsed(const QString& list) {
  ProcessingOptions::Formats formats;
  BOOST_REQUIRE(CommandLineRun::parseFormats(list, &formats));
  return formats;
}

ProcessingOptions::Formats formatsOf(const bool pdf, const bool txt, const bool hocr, const bool alto) {
  ProcessingOptions::Formats formats;
  formats.pdf = pdf;
  formats.txt = txt;
  formats.hocr = hocr;
  formats.alto = alto;
  return formats;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CommandLineRunTestSuite)

BOOST_AUTO_TEST_CASE(test_processes_and_saves_the_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(!out.isEmpty());
  BOOST_CHECK_EQUAL(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).size(), 1);
  // The project knows the output now, so the app doesn't take it as outdated.
  const ProjectReader::FileResult saved = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(saved.reader);
  const ProjectSession loaded = ProjectSession::load(*saved.reader, DefaultParams(), PageSelectionAccessor(nullptr));
  const PageId page(ImageId(dir.filePath("page.png")), PageId::SINGLE_PAGE);
  BOOST_CHECK(loaded.stages->outputFilter()->settings()->getOutputParams(page) != nullptr);
}

BOOST_AUTO_TEST_CASE(test_unreadable_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(dir.filePath("missing.ScanTailor"), &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!err.isEmpty());

  BOOST_REQUIRE(Tests::writeFile(dir.filePath("broken.ScanTailor"), "<project"));
  err.clear();
  BOOST_CHECK_EQUAL(run(dir.filePath("broken.ScanTailor"), &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!err.isEmpty());
}

BOOST_AUTO_TEST_CASE(test_project_of_an_older_version_is_left_to_the_app) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  // Version 4 is that of ScanTailor Advanced; the root element carries the first version attribute.
  QString xml = QString::fromUtf8(Tests::readFile(projectFile));
  const QRegularExpressionMatch match = QRegularExpression("version=\"\\d+\"").match(xml);
  BOOST_REQUIRE(match.hasMatch());
  xml.replace(match.capturedStart(), match.capturedLength(), "version=\"4\"");
  BOOST_REQUIRE(Tests::writeFile(projectFile, xml.toUtf8()));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_images_without_dpi_are_named) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 0);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(dir.filePath("page.png"))));
}

// The app would offer relinking for these; the tool must neither fail page by page nor create the
// old output folder anew.
BOOST_AUTO_TEST_CASE(test_project_that_needs_relinking_is_left_to_the_app) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300, QStringList(), "elsewhere/out");
  BOOST_REQUIRE(!projectFile.isEmpty());
  // As if the project had been copied to a computer without the folder of the output folder.
  BOOST_REQUIRE(QDir(dir.filePath("elsewhere")).removeRecursively());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(!QFileInfo::exists(dir.filePath("elsewhere")));

  // The output folder is there, but the image is missing.
  QTemporaryDir dir2;
  BOOST_REQUIRE(dir2.isValid());
  const QString projectFile2 = createProject(dir2, 300);
  BOOST_REQUIRE(!projectFile2.isEmpty());
  BOOST_REQUIRE(QFile::remove(dir2.filePath("page.png")));
  err.clear();
  BOOST_CHECK_EQUAL(run(projectFile2, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!err.isEmpty());
}

BOOST_AUTO_TEST_CASE(test_formats_are_read_from_a_list) {
  BOOST_CHECK(parsed("pdf,txt,alto") == formatsOf(true, true, false, true));
  BOOST_CHECK(parsed("pdf+txt+alto") == formatsOf(true, true, false, true));
  BOOST_CHECK(parsed("hocr") == formatsOf(false, false, true, false));
  BOOST_CHECK(parsed("TXT, pdf") == formatsOf(true, true, false, false));

  ProcessingOptions::Formats formats;
  for (const char* list : {"", ",", "doc", "pdf,doc", "pdf,", "pdf txt"}) {
    BOOST_CHECK_MESSAGE(!CommandLineRun::parseFormats(QString::fromLatin1(list), &formats), list);
  }
}

BOOST_AUTO_TEST_CASE(test_wrong_export_options_are_refused_before_anything_else) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  const auto refused = [&](const std::optional<QString>& folder, const std::optional<QString>& formats,
                           const std::optional<QString>& name) {
    QString err;
    const bool usageError = run(projectFile, &out, &err, folder, formats, name) == CommandLineRun::USAGE_ERROR;
    return usageError && !err.isEmpty();
  };
  const QString folder = dir.path();
  BOOST_CHECK(refused(QString(), std::nullopt, std::nullopt));
  BOOST_CHECK(refused(std::nullopt, QStringLiteral("doc"), std::nullopt));
  BOOST_CHECK(refused(std::nullopt, std::nullopt, QStringLiteral("nul")));
  BOOST_CHECK(refused(folder, QString(), std::nullopt));
  BOOST_CHECK(refused(folder, QStringLiteral("doc"), std::nullopt));
  // No valid name.
  BOOST_CHECK(refused(folder, std::nullopt, QString()));
  BOOST_CHECK(refused(folder, std::nullopt, QStringLiteral("nul")));
  BOOST_CHECK(refused(folder, std::nullopt, QStringLiteral("book.")));
  BOOST_CHECK(refused(folder, std::nullopt, QStringLiteral("sub/book")));
  // No folder to put the files into. A file is no folder, and nothing creates a missing one.
  BOOST_CHECK(refused(dir.filePath("missing"), std::nullopt, std::nullopt));
  BOOST_CHECK(!QFile::exists(dir.filePath("missing")));
  BOOST_CHECK(refused(dir.filePath("page.png"), std::nullopt, std::nullopt));
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
  BOOST_CHECK(!QFile::exists(dir.filePath("project.pdf")));
}

BOOST_AUTO_TEST_CASE(test_every_export_option_turns_on_the_export_next_to_the_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString pdfFile = dir.filePath("project.pdf");

  QString out;
  QString err;
  // Without any, only the processing.
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK(!QFile::exists(pdfFile));

  // The switch alone: the formats of the project, next to the project file and named like it.
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.exportRequested = true;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(QFile::exists(pdfFile));
  BOOST_REQUIRE(QFile::remove(pdfFile));

  // Each of the others turns it on as well, with the rest from the project.
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, QStringLiteral("pdf")), CommandLineRun::SUCCESS);
  BOOST_CHECK(QFile::exists(pdfFile));
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, QStringLiteral("band1")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(QFile::exists(dir.filePath("band1.pdf")));
}

BOOST_AUTO_TEST_CASE(test_exported_files_are_named_like_the_project_unless_a_name_is_given) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(QDir(dir.path()).mkdir("target"));
  const QDir target(dir.filePath("target"));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, target.path()), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(QFile::exists(target.filePath("project.pdf")));
  // Told before the processing, so a wrong name is noticed in time.
  const QString pdfFile = QDir::toNativeSeparators(target.filePath("project.pdf"));
  const qsizetype announced = out.indexOf(pdfFile);
  BOOST_REQUIRE(announced >= 0);
  BOOST_CHECK(announced < out.indexOf(QDir::toNativeSeparators(dir.filePath("out"))));

  // A separator at the end of the folder changes nothing.
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, target.path() + "/", std::nullopt, QStringLiteral("band1")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(QFile::exists(target.filePath("band1.pdf")));
}

// A typing error in --export must not destroy a file of the project.
BOOST_AUTO_TEST_CASE(test_export_must_not_replace_the_project_or_an_image) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString created = createProject(dir, 300);
  BOOST_REQUIRE(!created.isEmpty());

  // A project file and an image with the names of exported files.
  const QString projectFile = dir.filePath("book.txt");
  BOOST_REQUIRE(QFile::rename(created, projectFile));
  BOOST_REQUIRE(QFile::rename(dir.filePath("page.png"), dir.filePath("page.pdf")));
  QString xml = QString::fromUtf8(Tests::readFile(projectFile));
  BOOST_REQUIRE(xml.contains("page.png"));
  xml.replace("page.png", "page.pdf");
  BOOST_REQUIRE(Tests::writeFile(projectFile, xml.toUtf8()));
  const QByteArray project = Tests::readFile(projectFile);
  const QByteArray image = Tests::readFile(dir.filePath("page.pdf"));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("txt")),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(projectFile)));
  BOOST_CHECK(Tests::readFile(projectFile) == project);

  err.clear();
  // Written differently, but the same file.
  const QString imageAgain = QDir::toNativeSeparators(dir.path() + "/./page");
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, imageAgain, QStringLiteral("pdf")), CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(Tests::readFile(dir.filePath("page.pdf")) == image);
  // Refused before processing.
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_alto_folder_must_not_hold_a_file_of_the_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300, QStringList(), "book_alto");
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString inFolder = dir.filePath("book_alto/project.ScanTailor");
  BOOST_REQUIRE(QFile::copy(projectFile, inFolder));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(inFolder, &out, &err, dir.filePath("book"), QStringLiteral("alto")),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(err.contains(QStringLiteral("book_alto")));
  BOOST_CHECK(QFile::exists(inFolder));
}

BOOST_AUTO_TEST_CASE(test_project_without_a_format_needs_the_option) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300, QStringList(), "out", formatsOf(false, false, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(err.contains(QStringLiteral("--export-formats")));
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());

  err.clear();
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(Tests::readFile(dir.filePath("book.pdf")).startsWith("%PDF-"));
}

BOOST_AUTO_TEST_CASE(test_without_the_option_the_formats_of_the_project_count) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // No PDF, and text formats without a language, so the export fails because of the text formats.
  const QString projectFile = createProject(dir, 300, QStringList(), "out", formatsOf(false, true, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::EXPORT_FAILED);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(!QFile::exists(dir.filePath("book.txt")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.pdf")));

  // The option overrides them.
  err.clear();
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(QFile::exists(dir.filePath("book.pdf")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.txt")));
}

BOOST_AUTO_TEST_CASE(test_only_the_chosen_formats_are_created) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  // An extension as someone may type it belongs to no format of its own.
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book.pdf")), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(out.contains(QDir::toNativeSeparators(dir.filePath("book.pdf"))));
  BOOST_CHECK(QFile::exists(dir.filePath("book.pdf")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.pdf.pdf")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.txt")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.hocr")));
  BOOST_CHECK(!QFile::exists(dir.filePath("book_alto")));
}

BOOST_AUTO_TEST_CASE(test_existing_file_is_replaced_only_after_success) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString pdfFile = dir.filePath("book.pdf");
  BOOST_REQUIRE(Tests::writeFile(pdfFile, "old"));

  QString out;
  QString err;
  // Fails, as the project has no language for the text format.
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf,txt")),
                    CommandLineRun::EXPORT_FAILED);
  BOOST_CHECK(Tests::readFile(pdfFile) == "old");

  err.clear();
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(Tests::readFile(pdfFile).startsWith("%PDF-"));
}

BOOST_AUTO_TEST_CASE(test_cancelled) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  const std::atomic<bool> cancel(true);
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, cancel),
                    CommandLineRun::CANCELLED);
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_creates_the_pdf) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  const QString pdfFile = dir.filePath("book.pdf");
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(Tests::readFile(pdfFile).startsWith("%PDF-"));
}

BOOST_AUTO_TEST_CASE(test_no_pdf_if_a_page_failed) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  // The image is broken, so the page can't be loaded. A missing image would refuse the project.
  BOOST_REQUIRE(Tests::writeFile(dir.filePath("page.png"), "not an image"));

  QString out;
  QString err;
  const QString pdfFile = dir.filePath("book.pdf");
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::PAGES_FAILED);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(dir.filePath("page.png"))));
  BOOST_CHECK(!QFile::exists(pdfFile));
}

BOOST_AUTO_TEST_CASE(test_no_pdf_if_ocr_languages_are_missing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // A language that isn't installed anywhere.
  const QString projectFile = createProject(dir, 300, QStringList{"zzy"});
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  const QString pdfFile = dir.filePath("book.pdf");
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::EXPORT_FAILED);
  BOOST_CHECK(err.contains("zzy"));
  BOOST_CHECK(!QFile::exists(pdfFile));

  // The same for a text format without a PDF.
  err.clear();
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("txt")),
                    CommandLineRun::EXPORT_FAILED);
  BOOST_CHECK(err.contains("zzy"));
  BOOST_CHECK(!QFile::exists(dir.filePath("book.txt")));
}

BOOST_AUTO_TEST_SUITE_END()
