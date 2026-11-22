// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <CommandLineRun.h>
#include <DefaultParams.h>
#include <DefaultParamsProfileManager.h>
#include <ImageFileInfo.h>
#include <ImageId.h>
#include <PageId.h>
#include <PageSequence.h>
#include <ProcessingOptions.h>
#include <ProjectFiles.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectSession.h>
#include <StageSequence.h>
#include <filters/output/Settings.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);
  ProcessingOptions& options = *session.stages->processingOptions();
  options.pdf.textLayer = !ocrLanguages.isEmpty();
  options.ocr.languages = ocrLanguages;
  options.formats = formats;
  const QString projectFile = dir.filePath("project.ScanTailor");
  return session.write(projectFile) ? projectFile : QString();
}

/**
 * Runs the tool on \p projectFile; what it writes goes to \p out and \p err. The export options
 * turn on the export as --last-step export does; \p lastStep is what to pass otherwise.
 */
CommandLineRun::ExitCode run(const QString& projectFile,
                             QString* out,
                             QString* err,
                             const std::optional<QString>& exportDir = std::nullopt,
                             const std::optional<QString>& formats = std::nullopt,
                             const std::optional<QString>& exportName = std::nullopt,
                             const std::atomic<bool>& cancel = std::atomic<bool>(false),
                             CommandLineRun::Options::LastStep lastStep = CommandLineRun::Options::OUTPUT) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.lastStep = (exportDir || formats || exportName) ? CommandLineRun::Options::EXPORT : lastStep;
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

/**
 * Runs the tool on the folder \p inputFolder as the new project of it. The options are the folder
 * mode's: \p projectFile is --project-file; \p dpi, \p dpiFix and \p profile are --dpi,
 * --dpi-fix and --profile; \p colorModeDetect is --color-mode-detect.
 */
CommandLineRun::ExitCode runFolder(const QString& inputFolder,
                                   QString* out,
                                   QString* err,
                                   const std::optional<QString>& projectFile = std::nullopt,
                                   const std::optional<Dpi>& dpi = std::nullopt,
                                   const std::optional<Dpi>& dpiFix = std::nullopt,
                                   const QString& profile = QString(),
                                   const bool colorModeDetect = false) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  CommandLineRun::Options options;
  options.inputFolder = inputFolder;
  options.projectFile = projectFile ? *projectFile : QString();
  options.dpi = dpi;
  options.dpiFix = dpiFix;
  options.profile = profile;
  options.colorModeDetect = colorModeDetect;
  return CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false));
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
  const ProjectSession loaded = ProjectSession::load(*saved.reader, DefaultParams());
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

// The project is opened without the images that can't be read. Saved by the tool, they would be gone
// from the file without a word, so the project is left to the app, which offers a backup copy.
BOOST_AUTO_TEST_CASE(test_project_with_a_damaged_image_is_left_to_the_app) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  // A second image whose file isn't in the project.
  QString xml = QString::fromUtf8(Tests::readFile(projectFile));
  BOOST_REQUIRE(xml.contains("</images>"));
  xml.replace("</images>", "<image id=\"99\" subPages=\"1\" fileId=\"99\" fileImage=\"0\"/></images>");
  const QByteArray damaged = xml.toUtf8();
  BOOST_REQUIRE(Tests::writeFile(projectFile, damaged));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains("1 image"));
  BOOST_CHECK(Tests::readFile(projectFile) == damaged);
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
  options.lastStep = CommandLineRun::Options::EXPORT;
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

// --last-step margins: the run stops after the Margins step, without output and without an error.
BOOST_AUTO_TEST_CASE(test_last_step_margins_succeeds_without_output) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::MARGINS),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(err.isEmpty());
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

// --last-step export: the export runs with the formats the project has ticked.
BOOST_AUTO_TEST_CASE(test_last_step_export_exports_with_the_project_formats) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile
      = createProject(dir, 300, QStringList(), QStringLiteral("out"), formatsOf(true, false, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::EXPORT),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(QFile::exists(dir.filePath("project.pdf")));
}

// The export options lead the run to the export; an explicit earlier last step is a usage error.
BOOST_AUTO_TEST_CASE(test_export_options_need_the_export_step) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  for (const CommandLineRun::Options::LastStep lastStep :
       {CommandLineRun::Options::FIX_ORIENTATION, CommandLineRun::Options::SPLIT_PAGES, CommandLineRun::Options::DESKEW,
        CommandLineRun::Options::SELECT_CONTENT, CommandLineRun::Options::MARGINS, CommandLineRun::Options::OUTPUT}) {
    CommandLineRun::Options options;
    options.projectFile = projectFile;
    options.lastStep = lastStep;
    options.exportDir = dir.filePath("out");
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                      CommandLineRun::USAGE_ERROR);
    BOOST_CHECK(!err.isEmpty());
  }
  BOOST_CHECK(!QFile::exists(dir.filePath("project.pdf")));
}

// With --export too, a moved project is left to the app, and not refused for a wrong command line.
BOOST_AUTO_TEST_CASE(test_moved_project_with_export_is_left_to_the_app) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(QFile::remove(dir.filePath("page.png")));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, QStringLiteral("pdf")),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK_MESSAGE(!err.contains(QStringLiteral("replace")), err.toStdString());

  err.clear();
  BOOST_CHECK_EQUAL(run(dir.filePath("missing/project.ScanTailor"), &out, &err, std::nullopt, QStringLiteral("pdf")),
                    CommandLineRun::PROJECT_UNUSABLE);
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

// The same rule as in the app: no spaces around the name, no extension of a format.
BOOST_AUTO_TEST_CASE(test_default_name_follows_the_rule_of_the_app) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString created = createProject(dir, 300);
  BOOST_REQUIRE(!created.isEmpty());
  BOOST_REQUIRE(QDir(dir.path()).mkdir("target"));
  const QDir target(dir.filePath("target"));

  QString out;
  QString err;
  const QString withExtension = dir.filePath("Book.pdf.ScanTailor");
  BOOST_REQUIRE(QFile::rename(created, withExtension));
  BOOST_CHECK_EQUAL(run(withExtension, &out, &err, target.path()), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(target.entryList(QDir::Files) == QStringList{"Book.pdf"});

  const QString withSpace = dir.filePath("Volume .ScanTailor");
  BOOST_REQUIRE(QFile::rename(withExtension, withSpace));
  err.clear();
  BOOST_CHECK_EQUAL(run(withSpace, &out, &err, target.path()), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(target.exists("Volume.pdf"));
  err.clear();

  BOOST_CHECK_EQUAL(run(withSpace, &out, &err, target.path(), std::nullopt, QStringLiteral(" Part 2.pdf ")),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(target.exists("Part 2.pdf"));
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

// ---- The folder mode ----

// The files are taken in SmartFilenameOrdering ("9.png" before "010.png", where the plain text
// order takes "010.png" first), the subfolder out/ of the default output folder is not read, and
// other files are only counted.
BOOST_AUTO_TEST_CASE(test_folder_mode_files_in_smart_order) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir + QStringLiteral("/out")));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/010.png"), 300).imageInfo().empty());
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/9.png"), 300).imageInfo().empty());
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/out/sneaky.png"), 300).imageInfo().empty());
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/other.txt"), "text"));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::SUCCESS);
  const ProjectReader::FileResult read = ProjectReader::readFile(project_files::defaultProjectFile(inputDir));
  BOOST_REQUIRE(read.reader);
  const PageSequence pages = read.reader->pages()->toPageSequence(IMAGE_VIEW);
  BOOST_CHECK_EQUAL(pages.numPages(), 2);
  if (pages.numPages() == 2) {
    BOOST_CHECK(pages.pageAt(0).imageId().filePath().endsWith(QStringLiteral("9.png")));
    BOOST_CHECK(pages.pageAt(1).imageId().filePath().endsWith(QStringLiteral("010.png")));
  }
}

BOOST_AUTO_TEST_CASE(test_folder_mode_refuses_an_empty_folder) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("no images")));
}

BOOST_AUTO_TEST_CASE(test_folder_mode_refuses_unreadable_files) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/broken.png"), "not a png"));
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("broken.png")));
}

BOOST_AUTO_TEST_CASE(test_folder_mode_refuses_an_existing_project_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::SUCCESS);
  err.clear();
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(project_files::defaultProjectFile(inputDir))));
}

// Without an option a doubtful resolution is accepted, with a warning that lists the file.
BOOST_AUTO_TEST_CASE(test_folder_mode_yellow_dpi_warns) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 0).imageInfo().empty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK(err.contains(QStringLiteral("page.png")));
  const ProjectReader::FileResult read = ProjectReader::readFile(project_files::defaultProjectFile(inputDir));
  BOOST_REQUIRE(read.reader);
  BOOST_CHECK(read.reader->pages()->toPageSequence(IMAGE_VIEW).pageAt(0).metadata().isDpiOK());
}

// --dpi sets the DPI of all images; the red pages are refused without it, and a --dpi value that
// is red for an image is refused as well.
BOOST_AUTO_TEST_CASE(test_folder_mode_dpi_option) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 1).imageInfo().empty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, dir.filePath("p1.ScanTailor"), Dpi(300, 300)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("DPI: 300 dpi")));
  const ProjectReader::FileResult read = ProjectReader::readFile(dir.filePath("p1.ScanTailor"));
  BOOST_REQUIRE(read.reader);
  BOOST_CHECK(read.reader->pages()->toPageSequence(IMAGE_VIEW).pageAt(0).metadata().dpi() == Dpi(300, 300));

  // A value that is red for an image is refused.
  QTemporaryDir dir2;
  BOOST_REQUIRE(dir2.isValid());
  const QString inputDir2 = dir2.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir2));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir2 + QStringLiteral("/page.png"), 1).imageInfo().empty());
  err.clear();
  BOOST_CHECK_EQUAL(runFolder(inputDir2, &out, &err, dir2.filePath("p2.ScanTailor"), Dpi(0, 0)),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("red")));
}

// The doubtful pages are accepted without an option, with a warning; --dpi-fix sets the DPI of
// the pages that need fixing.
BOOST_AUTO_TEST_CASE(test_folder_mode_dpi_fix_option) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 72).imageInfo().empty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK(err.contains(QStringLiteral("doubtful")));
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, dir.filePath("p1.ScanTailor"), std::nullopt, Dpi(300, 300)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("Fixed the DPI of 1 file(s) to 300 dpi")));
  const ProjectReader::FileResult read = ProjectReader::readFile(dir.filePath("p1.ScanTailor"));
  BOOST_REQUIRE(read.reader);
  BOOST_CHECK(read.reader->pages()->toPageSequence(IMAGE_VIEW).pageAt(0).metadata().dpi() == Dpi(300, 300));

  // A value that is red for the page is refused.
  QTemporaryDir dir2;
  BOOST_REQUIRE(dir2.isValid());
  const QString inputDir2 = dir2.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir2));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir2 + QStringLiteral("/page.png"), 0).imageInfo().empty());
  err.clear();
  BOOST_CHECK_EQUAL(runFolder(inputDir2, &out, &err, std::nullopt, std::nullopt, Dpi(0, 0)),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("red")));
}

// The built-in profiles are used by name, "Source" is the old name of "Minimal"; a profile that
// isn't there is refused with its name.
BOOST_AUTO_TEST_CASE(test_folder_mode_profile_by_name) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());

  const QPair<QString, QString> cases[] = {
      {QStringLiteral("Default"), QStringLiteral("Default")},
      {QStringLiteral("Minimal"), QStringLiteral("Minimal")},
      {QStringLiteral("Source"), QStringLiteral("Minimal")},
  };
  for (const QPair<QString, QString>& c : cases) {
    QString out;
    QString err;
    BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, dir.filePath("p_" + c.first + ".ScanTailor"), std::nullopt,
                                std::nullopt, c.first),
                      CommandLineRun::SUCCESS);
    BOOST_CHECK(out.contains(QStringLiteral("Profile: ") + c.second));
  }

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, dir.filePath("p_nope.ScanTailor"), std::nullopt, std::nullopt,
                              QStringLiteral("NoSuchProfile")),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("NoSuchProfile")));
}

// A profile the tool reads from a .stp file; one that can't be read is refused.
BOOST_AUTO_TEST_CASE(test_folder_mode_profile_by_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());

  const QString profilesDir = dir.filePath("profiles");
  BOOST_REQUIRE(QDir().mkpath(profilesDir));
  BOOST_REQUIRE(DefaultParamsProfileManager(profilesDir).writeProfile(DefaultParams(), QStringLiteral("TestProfile")));
  const QString profileFile = profilesDir + QStringLiteral("/TestProfile.stp");
  BOOST_REQUIRE(QFile::exists(profileFile));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(
      runFolder(inputDir, &out, &err, dir.filePath("p.ScanTailor"), std::nullopt, std::nullopt, profileFile),
      CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("Profile: ") + profileFile));

  err.clear();
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, dir.filePath("p2.ScanTailor"), std::nullopt, std::nullopt,
                              dir.filePath("NoSuchProfile.stp")),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("NoSuchProfile")));
}

/** Saves a page with a colored picture above black bars, as the color page of the end-to-end test. */
QString saveColorPageScan(const QString& path) {
  QImage page(QSize(1200, 1700), QImage::Format_RGB32);
  page.fill(Qt::white);
  {
    QPainter painter(&page);
    const QRect pictureRect(200, 250, 800, 500);
    for (int y = pictureRect.top(); y < pictureRect.bottom(); y += 25) {
      for (int x = pictureRect.left(); x < pictureRect.right(); x += 25) {
        const int v = 40 + ((x * 13 + y * 7) % 170);
        painter.fillRect(QRect(x, y, 25, 25), QColor(v, 220 - v, 60 + (v * 3) % 150));
      }
    }
    const QRect textArea(pictureRect.left(), pictureRect.bottom() + 100, pictureRect.width(), 600);
    for (int y = textArea.top(); y + 6 <= textArea.bottom(); y += 18) {
      int x = textArea.left();
      while (true) {
        const int width = 20 + ((x * 7 + y * 3) % 40);
        if (x + width > textArea.right()) {
          break;
        }
        painter.fillRect(QRect(x, y, width, 6), Qt::black);
        x += width + 12;
      }
    }
  }
  const Dpm dpm(Dpi(300, 300));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  return page.save(path) ? path : QString();
}

BOOST_AUTO_TEST_CASE(test_folder_mode_color_mode_detect) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  const QString pageFile = saveColorPageScan(inputDir + QStringLiteral("/page.png"));
  BOOST_REQUIRE(!pageFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), true),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("Color mode detected for 1 page(s)")));
  const ProjectReader::FileResult read = ProjectReader::readFile(project_files::defaultProjectFile(inputDir));
  BOOST_REQUIRE(read.reader);
  const ProjectSession session = ProjectSession::load(*read.reader, DefaultParams());
  const PageId pageId(ImageId(pageFile), PageId::SINGLE_PAGE);
  BOOST_CHECK(session.stages->outputFilter()->settings()->getParams(pageId).colorParams().colorMode() == output::MIXED);
}

// --threads: the run gets its budget; a too small value is a usage error.
BOOST_AUTO_TEST_CASE(test_threads_option) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.threads = 0;
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(!err.isEmpty());

  options.threads = 2;
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
}

// The report of a run: valid JSON with the fields of the schema of --report-file.
BOOST_AUTO_TEST_CASE(test_report_file_is_valid_json_with_the_schema) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");

  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.lastStep = CommandLineRun::Options::MARGINS;
  options.reportFile = reportFile;
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(Tests::readFile(reportFile), &parseError);
  BOOST_CHECK(parseError.error == QJsonParseError::NoError);
  const QJsonObject root = doc.object();
  BOOST_CHECK_EQUAL(root["schemaVersion"].toInt(), 1);
  const QJsonObject run = root["run"].toObject();
  BOOST_CHECK(run.contains("mode") && run.contains("projectFile") && run.contains("profile") && run.contains("lastStep")
              && run.contains("exitCode") && run.contains("cancelled"));
  BOOST_CHECK(run["mode"].toString() == QStringLiteral("project"));
  BOOST_CHECK(run["lastStep"].toString() == QStringLiteral("margins"));
  BOOST_CHECK_EQUAL(run["exitCode"].toInt(), CommandLineRun::SUCCESS);
  BOOST_REQUIRE_EQUAL(root["pages"].toArray().size(), 1);
  const QJsonObject page = root["pages"].toArray()[0].toObject();
  BOOST_CHECK(page.contains("file") && page.contains("dpi") && page.contains("dpiSource") && page.contains("colorMode")
              && page.contains("colorModeDetected") && page.contains("outputFile") && page.contains("status"));
  BOOST_CHECK(page["status"].toString() == QStringLiteral("ok"));
}

BOOST_AUTO_TEST_SUITE_END()