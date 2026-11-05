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

#include "TestHelpers.h"

namespace {
/**
 * A project of one scanned page, saved in \p dir as the app saves a new one, with its output
 * going to \p outDir there. With \p dpi 0, the page lacks its resolution. Its PDF gets the text
 * recognition with \p ocrLanguages, none without; by default, a project would recognize German
 * and English, which needn't be installed here. Returns the project file, or an empty string if
 * it couldn't be created.
 */
QString createProject(const QTemporaryDir& dir,
                      const int dpi,
                      const QStringList& ocrLanguages = QStringList(),
                      const QString& outDirName = QStringLiteral("out")) {
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
  const QString projectFile = dir.filePath("project.ScanTailor");
  return session.write(projectFile) ? projectFile : QString();
}

/** Runs the tool on \p projectFile; what it writes goes to \p out and \p err. */
CommandLineRun::ExitCode run(const QString& projectFile,
                             QString* out,
                             QString* err,
                             const QString& pdfFile = QString(),
                             const std::atomic<bool>& cancel = std::atomic<bool>(false)) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.pdfFile = pdfFile;
  return CommandLineRun::run(options, outStream, errStream, cancel);
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

// A typing error in --pdf must not destroy a file of the project.
BOOST_AUTO_TEST_CASE(test_pdf_must_not_replace_the_project_or_an_image) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QByteArray project = Tests::readFile(projectFile);
  const QByteArray image = Tests::readFile(dir.filePath("page.png"));

  QString out;
  QString err;
  // Written differently, but the same file.
  const QString imageAgain = QDir::toNativeSeparators(dir.path() + "/./page.png");
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, imageAgain), CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(Tests::readFile(dir.filePath("page.png")) == image);

  err.clear();
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, projectFile), CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(!err.isEmpty());
  BOOST_CHECK(Tests::readFile(projectFile) == project);
  // Refused before processing.
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_cancelled) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  const std::atomic<bool> cancel(true);
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, QString(), cancel), CommandLineRun::CANCELLED);
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
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, pdfFile), CommandLineRun::SUCCESS);
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
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, pdfFile), CommandLineRun::PAGES_FAILED);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(dir.filePath("page.png"))));
  BOOST_CHECK(!QFile::exists(pdfFile));
}

#ifdef ENABLE_OCR
BOOST_AUTO_TEST_CASE(test_no_pdf_if_ocr_languages_are_missing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // A language that isn't installed anywhere.
  const QString projectFile = createProject(dir, 300, QStringList{"zzy"});
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  const QString pdfFile = dir.filePath("book.pdf");
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, pdfFile), CommandLineRun::PDF_FAILED);
  BOOST_CHECK(err.contains("zzy"));
  BOOST_CHECK(!QFile::exists(pdfFile));
}
#endif

BOOST_AUTO_TEST_SUITE_END()
