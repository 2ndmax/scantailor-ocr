// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ApplicationSettings.h>
#include <CommandLineRun.h>
#include <DefaultParams.h>
#include <DefaultParamsProfileManager.h>
#include <DefaultParamsProvider.h>
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
#include <ThreadBudget.h>
#include <filters/fix_orientation/Settings.h>
#include <filters/output/Settings.h>

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <atomic>
#include <boost/test/unit_test.hpp>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <tiffio.h>
}

#include "ScopedMaxThreads.h"
#include "SmartFilenameOrdering.h"
#include "TestHelpers.h"
#include "TestTiffFile.h"

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
                             CommandLineRun::Options::LastStep lastStep = CommandLineRun::Options::OUTPUT,
                             const std::optional<QString>& reportFile = std::nullopt,
                             QString* progressJson = nullptr) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  QTextStream progressStream(progressJson);
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.lastStep = (exportDir || formats || exportName) ? CommandLineRun::Options::EXPORT : lastStep;
  options.exportDir = exportDir;
  options.exportName = exportName;
  options.formats = formats;
  options.reportFile = reportFile;
  return CommandLineRun::run(options, outStream, errStream, cancel, progressJson ? &progressStream : nullptr);
}

/** Exports into the folder of \p file under its name, with --export-folder and --export-name. */
CommandLineRun::ExitCode runExport(const QString& projectFile,
                                   QString* out,
                                   QString* err,
                                   const QString& file,
                                   const std::optional<QString>& formats = std::nullopt,
                                   const std::optional<QString>& reportFile = std::nullopt) {
  const QFileInfo info(file);
  return run(projectFile, out, err, info.path(), formats, info.fileName(), std::atomic<bool>(false),
             CommandLineRun::Options::EXPORT, reportFile);
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
                                   const bool colorModeDetect = false,
                                   const QString& orientationDetect = QString(),
                                   const std::optional<QString>& reportFile = std::nullopt,
                                   QString* progressJson = nullptr) {
  QTextStream outStream(out);
  QTextStream errStream(err);
  QTextStream progressStream(progressJson);
  CommandLineRun::Options options;
  options.inputFolder = inputFolder;
  options.projectFile = projectFile ? *projectFile : QString();
  options.dpi = dpi;
  options.dpiFix = dpiFix;
  options.profile = profile;
  options.colorModeDetect = colorModeDetect;
  options.orientationDetect = orientationDetect;
  options.reportFile = reportFile;
  return CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false),
                             progressJson ? &progressStream : nullptr);
}

/** Reads the JSON \p file of --report-file; the caller checks its contents. */
QJsonObject readReport(const QString& file) {
  QJsonParseError parseError;
  const QJsonDocument doc = QJsonDocument::fromJson(Tests::readFile(file), &parseError);
  BOOST_CHECK(parseError.error == QJsonParseError::NoError);
  return doc.object();
}

/** Splits the JSON Lines of a --progress-json output into objects; fails on any bad or empty line. */
std::vector<QJsonObject> parseProgress(const QString& text) {
  std::vector<QJsonObject> result;
  QStringList lines = text.split(QLatin1Char('\n'));
  if (!lines.isEmpty() && lines.back().isEmpty()) {
    lines.pop_back();
  }
  for (const QString& line : lines) {
    BOOST_REQUIRE(!line.isEmpty());
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line.toUtf8(), &parseError);
    BOOST_REQUIRE(parseError.error == QJsonParseError::NoError);
    BOOST_REQUIRE(doc.isObject());
    const QJsonObject object = doc.object();
    BOOST_REQUIRE(object.contains(QStringLiteral("schemaVersion")));
    BOOST_REQUIRE(object.contains(QStringLiteral("event")));
    result.push_back(object);
  }
  return result;
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

/** Writes a small three-page TIFF whose second directory points past the end of the file. */
bool writePartlyReadableTiff(const QString& path) {
  TIFF* tif = Tests::openTiffFile(path, "wl");
  if (tif == nullptr) {
    return false;
  }
  bool ok = true;
  for (int page = 0; (page < 3) && ok; ++page) {
    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(64));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(64));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
    TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, static_cast<uint16_t>(SAMPLEFORMAT_UINT));
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_MINISBLACK));
    TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG));
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(64));
    TIFFSetField(tif, TIFFTAG_XRESOLUTION, 300.0f);
    TIFFSetField(tif, TIFFTAG_YRESOLUTION, 300.0f);
    TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, static_cast<uint16_t>(RESUNIT_INCH));
    std::vector<uint8_t> row(64, 255);
    for (int y = 0; (y < 64) && ok; ++y) {
      ok = TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), 0) >= 0;
    }
    ok = ok && TIFFWriteDirectory(tif);
  }
  TIFFClose(tif);
  if (!ok) {
    return false;
  }

  QByteArray data = Tests::readFile(path);
  const auto readLe = [&data](const uint32_t offset, const int bytes) {
    uint32_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) {
      value = (value << 8) | static_cast<uint8_t>(data[offset + i]);
    }
    return value;
  };
  const auto writeLe = [&data](const uint32_t offset, const int bytes, uint32_t value) {
    for (int i = 0; i < bytes; ++i, value >>= 8) {
      data[offset + i] = static_cast<char>(value & 0xff);
    }
  };
  const uint32_t firstIfd = readLe(4, 4);
  const uint32_t firstEntries = readLe(firstIfd, 2);
  const uint32_t secondIfd = readLe(firstIfd + 2 + 12 * firstEntries, 4);
  const uint32_t secondEntries = readLe(secondIfd, 2);
  // The four bytes after the entries of a directory hold the offset of the next one.
  writeLe(secondIfd + 2 + 12 * secondEntries, 4, static_cast<uint32_t>(data.size()) + 4096);
  return Tests::writeFile(path, data);
}

/** Writes one RGB page of a test TIFF, with the resolution the scans of these tests have. */
bool writeTiffPage(TIFF* tif, const QImage& source) {
  const QImage image = source.convertToFormat(QImage::Format_RGB888);
  if (image.isNull()) {
    return false;
  }
  bool ok = TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(image.width()))
            && TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(image.height()))
            && TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(8))
            && TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(3))
            && TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, static_cast<uint16_t>(PHOTOMETRIC_RGB))
            && TIFFSetField(tif, TIFFTAG_COMPRESSION, static_cast<uint16_t>(COMPRESSION_NONE))
            && TIFFSetField(tif, TIFFTAG_PLANARCONFIG, static_cast<uint16_t>(PLANARCONFIG_CONTIG))
            && TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(image.height()))
            && TIFFSetField(tif, TIFFTAG_XRESOLUTION, 300.0f) && TIFFSetField(tif, TIFFTAG_YRESOLUTION, 300.0f)
            && TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, static_cast<uint16_t>(RESUNIT_INCH));
  for (int y = 0; ok && (y < image.height()); ++y) {
    ok = TIFFWriteEncodedStrip(tif, static_cast<uint32_t>(y), const_cast<uchar*>(image.constScanLine(y)),
                               static_cast<tmsize_t>(image.bytesPerLine()))
         >= 0;
  }
  return ok && (TIFFWriteDirectory(tif) == 1);
}

/** A two-page TIFF: the first page is colored, the second one black and white. */
bool writeTwoPageTiff(const QString& path, const QImage& colorPage, const QImage& bwPage) {
  TIFF* tif = Tests::openTiffFile(path, "w");
  if (tif == nullptr) {
    return false;
  }
  const bool ok = writeTiffPage(tif, colorPage) && writeTiffPage(tif, bwPage);
  TIFFClose(tif);
  return ok;
}

/**
 * The profile of the program is global state. Keep every test on the built-in Default profile, also
 * when the machine running the tests has another one in its settings, and put the old state back.
 */
class ScopedDefaultProfile {
 public:
  ScopedDefaultProfile()
      : m_settings(ApplicationSettings::getInstance()),
        m_savedProfile(m_settings.getCurrentProfile()),
        m_savedParams(DefaultParamsProvider::getInstance().getParams()),
        m_savedName(DefaultParamsProvider::getInstance().getProfileName()) {
    DefaultParamsProvider::getInstance().setParams(std::make_unique<DefaultParams>(), QStringLiteral("Default"));
    m_settings.setCurrentProfile(QStringLiteral("Default"));
  }

  ~ScopedDefaultProfile() {
    m_settings.setCurrentProfile(m_savedProfile);
    DefaultParamsProvider::getInstance().setParams(std::make_unique<DefaultParams>(m_savedParams), m_savedName);
  }

  ScopedDefaultProfile(const ScopedDefaultProfile&) = delete;
  ScopedDefaultProfile& operator=(const ScopedDefaultProfile&) = delete;

 private:
  ApplicationSettings& m_settings;
  QString m_savedProfile;
  DefaultParams m_savedParams;
  QString m_savedName;
};
}  // namespace

BOOST_FIXTURE_TEST_SUITE(CommandLineRunTestSuite, ScopedDefaultProfile)

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
  // A file is no folder. A missing one is created, see test_export_folder_is_created.
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
// Folders the tool writes to are created; only folders it reads from have to exist.
BOOST_AUTO_TEST_CASE(test_export_folder_is_created) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile
      = createProject(dir, 300, QStringList(), QStringLiteral("out"), formatsOf(true, false, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString exportDir = dir.filePath("new/exports");
  BOOST_REQUIRE(!QFileInfo::exists(exportDir));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, exportDir, QStringLiteral("pdf")), CommandLineRun::SUCCESS);
  BOOST_CHECK(QFileInfo(exportDir).isDir());
  BOOST_CHECK(QFileInfo::exists(exportDir + QStringLiteral("/project.pdf")));
}

// A project file that can't be saved at the end is refused before the pages are processed.
BOOST_AUTO_TEST_CASE(test_read_only_project_file_is_refused_before_processing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(QFile::setPermissions(projectFile, QFileDevice::ReadOwner));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(projectFile)));
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
  QFile::setPermissions(projectFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// The input folder is checked before the project file in it, so the message names the folder.
BOOST_AUTO_TEST_CASE(test_folder_mode_names_a_missing_input_folder) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("missing");

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(
      runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Default")),
      CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("The input folder doesn't exist")));
}

// An output folder that can't be created is refused before the pages are processed.
BOOST_AUTO_TEST_CASE(test_output_folder_under_a_file_is_refused_before_processing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  const QString blocker = dir.filePath("not-a-folder");
  BOOST_REQUIRE(Tests::writeFile(blocker, "file"));

  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.profile = QStringLiteral("Default");
  options.outputFolder = blocker + QStringLiteral("/out");
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!QFileInfo::exists(project_files::defaultProjectFile(inputDir)));
}

// An export folder that can't be created is refused before the pages are processed.
BOOST_AUTO_TEST_CASE(test_export_folder_under_a_file_is_refused_before_processing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile
      = createProject(dir, 300, QStringList(), QStringLiteral("out"), formatsOf(true, false, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString blocker = dir.filePath("not-a-folder");
  BOOST_REQUIRE(Tests::writeFile(blocker, "file"));
  const QString exportDir = blocker + QStringLiteral("/exports");

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, exportDir, QStringLiteral("pdf")), CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(err.contains(QDir::toNativeSeparators(exportDir)));
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
}

// A report that can't be written at the end is refused before the pages are processed.
BOOST_AUTO_TEST_CASE(test_report_file_that_cant_be_written_is_refused_before_processing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  // A file where the folder of the report would be, and a read-only report file.
  const QString blocker = dir.filePath("not-a-folder");
  BOOST_REQUIRE(Tests::writeFile(blocker, "file"));
  const QString readOnlyReport = dir.filePath("report.json");
  BOOST_REQUIRE(Tests::writeFile(readOnlyReport, "{}"));
  BOOST_REQUIRE(QFile::setPermissions(readOnlyReport, QFileDevice::ReadOwner));

  for (const QString& reportFile : {blocker + QStringLiteral("/report.json"), readOnlyReport}) {
    QString out;
    QString err;
    BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                          CommandLineRun::Options::OUTPUT, reportFile),
                      CommandLineRun::USAGE_ERROR);
    BOOST_CHECK(!err.isEmpty());
    BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
  }
  QFile::setPermissions(readOnlyReport, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

// A refused project must not leave the output folder it asked for behind.
BOOST_AUTO_TEST_CASE(test_output_folder_is_not_created_for_a_refused_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/broken.png"), "not a png"));
  const QString outputDir = dir.filePath("new/out");
  BOOST_REQUIRE(!QFileInfo::exists(outputDir));

  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.outputFolder = outputDir;
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!QFileInfo::exists(outputDir));
}

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

// The export folder is created only once the project is usable; a failed one leaves no empty folder.
BOOST_AUTO_TEST_CASE(test_export_folder_is_not_created_for_a_refused_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(QFile::remove(dir.filePath("page.png")));
  const QString exportDir = dir.filePath("new/exports");
  BOOST_REQUIRE(!QFileInfo::exists(exportDir));

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, exportDir, QStringLiteral("pdf")), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(!QFileInfo::exists(exportDir));
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
  // No PDF, and a text format without a language: refused before processing, as text recognition needs one.
  const QString projectFile = createProject(dir, 300, QStringList(), "out", formatsOf(false, true, false, false));
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());
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
  // Refused before processing, as the project has no language for the text format. That an export
  // replaces the files only once it succeeded is tested with ExportJob, in TestExportFormats.
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf,txt")),
                    CommandLineRun::PROJECT_UNUSABLE);
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
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book")), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains("zzy"));
  BOOST_CHECK(!QFile::exists(pdfFile));
  // Refused before processing: the languages are known from the project.
  BOOST_CHECK(QDir(dir.filePath("out")).entryList({"*.tif"}, QDir::Files).isEmpty());

  // The same for a text format without a PDF.
  err.clear();
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("txt")),
                    CommandLineRun::PROJECT_UNUSABLE);
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
  BOOST_CHECK(out.contains(QStringLiteral("1 other file(s) passed over")));
  BOOST_CHECK(!out.contains(QStringLiteral("other.txt")));
  const ProjectReader::FileResult read = ProjectReader::readFile(project_files::defaultProjectFile(inputDir));
  BOOST_REQUIRE(read.reader);
  const PageSequence pages = read.reader->pages()->toPageSequence(IMAGE_VIEW);
  BOOST_CHECK_EQUAL(pages.numPages(), 2);
  if (pages.numPages() == 2) {
    BOOST_CHECK(pages.pageAt(0).imageId().filePath().endsWith(QStringLiteral("9.png")));
    BOOST_CHECK(pages.pageAt(1).imageId().filePath().endsWith(QStringLiteral("010.png")));
  }
}

// No line at all when every file of the folder became a page.
BOOST_AUTO_TEST_CASE(test_folder_mode_reports_no_passed_over_files) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK(!out.contains(QStringLiteral("passed over")));
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
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/broken2.png"), "also not a png"));
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("broken.png")));
  BOOST_CHECK(err.contains(QStringLiteral("broken2.png")));
}

// A file with a damaged page after readable ones is no more acceptable than an unreadable one.
BOOST_AUTO_TEST_CASE(test_folder_mode_refuses_partly_readable_files) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(writePartlyReadableTiff(inputDir + QStringLiteral("/partly.tif")));
  BOOST_REQUIRE(writePartlyReadableTiff(inputDir + QStringLiteral("/partly2.tif")));
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK_EQUAL(err.count(QStringLiteral("only partly readable")), 2);
  BOOST_CHECK(err.contains(QStringLiteral("Page 3 of this file is damaged.")));
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
  BOOST_CHECK(err.contains(QStringLiteral("page.png")));
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

  // A name that can't exist in the profile folder of this machine, whatever is stored there.
  const QString missingName = QStringLiteral("NoSuchProfile_") + QFileInfo(dir.path()).baseName();
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(
      runFolder(inputDir, &out, &err, dir.filePath("p_nope.ScanTailor"), std::nullopt, std::nullopt, missingName),
      CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(missingName));
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

BOOST_AUTO_TEST_CASE(test_folder_mode_refuses_an_unreadable_settings_profile) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());

  // The stored name differs from the one DefaultParamsProvider was started with, as startProfile()
  // reports an unreadable profile. The suite fixture puts the old setting back.
  const QString missingName = QStringLiteral("MissingSettingsProfile_") + QFileInfo(dir.path()).baseName();
  ApplicationSettings::getInstance().setCurrentProfile(missingName);

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("profile")));
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
  BOOST_CHECK_EQUAL(
      runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), true, QString()),
      CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("Color mode detected for 1 page(s)")));
  const ProjectReader::FileResult read = ProjectReader::readFile(project_files::defaultProjectFile(inputDir));
  BOOST_REQUIRE(read.reader);
  const ProjectSession session = ProjectSession::load(*read.reader, DefaultParams());
  const PageId pageId(ImageId(pageFile), PageId::SINGLE_PAGE);
  BOOST_CHECK(session.stages->outputFilter()->settings()->getParams(pageId).colorParams().colorMode() == output::MIXED);
}

// Detection before the Output pass: the output files must be made with the mode it found, so an
// export directly after them still sees them as current. A profile without OCR keeps the test
// independent of the languages installed on the machine.
BOOST_AUTO_TEST_CASE(test_folder_mode_color_detect_is_used_by_the_output) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!saveColorPageScan(inputDir + QStringLiteral("/page.png")).isEmpty());

  DefaultParams params;
  ProcessingOptions processing = params.getProcessingOptions();
  processing.pdf.textLayer = false;
  processing.ocr.languages.clear();
  params.setProcessingOptions(processing);
  const QString profilesDir = dir.filePath("profiles");
  BOOST_REQUIRE(QDir().mkpath(profilesDir));
  BOOST_REQUIRE(DefaultParamsProfileManager(profilesDir).writeProfile(params, QStringLiteral("NoOcr")));
  const QString projectFile = dir.filePath("p.ScanTailor");

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.projectFile = projectFile;
  options.profile = profilesDir + QStringLiteral("/NoOcr.stp");
  options.colorModeDetect = true;
  options.lastStep = CommandLineRun::Options::EXPORT;
  options.formats = QStringLiteral("pdf");
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(err.isEmpty());
  BOOST_CHECK(QFileInfo::exists(dir.filePath("p.pdf")));
}

// --threads: the run gets its budget, which is restored for the other tests; a too small value is
// a usage error and leaves the budget alone.
BOOST_AUTO_TEST_CASE(test_threads_option) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  const Tests::ScopedMaxThreads restoreThreads(ThreadBudget::maxThreads());
  const int saved = ThreadBudget::maxThreads();
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
  BOOST_CHECK_EQUAL(ThreadBudget::maxThreads(), saved);

  options.threads = 2;
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK_EQUAL(ThreadBudget::maxThreads(), std::min(2, ThreadBudget::availableThreads()));
}

BOOST_AUTO_TEST_CASE(test_threads_does_not_change_the_budget_on_a_usage_error) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  const Tests::ScopedMaxThreads restoreThreads(ThreadBudget::maxThreads());
  const int saved = ThreadBudget::maxThreads();
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.threads = 2;
  // Export options without --last-step export are a usage error.
  options.exportDir = dir.path();
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK_EQUAL(ThreadBudget::maxThreads(), saved);
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

// The two pages of one TIFF have different numbers and, after detection, different modes. Without
// the number in the key of a page, the report gives both pages the first one's mode.
BOOST_AUTO_TEST_CASE(test_report_distinguishes_pages_of_a_multi_page_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  const QString colorPath = dir.filePath("color.png");
  const QString bwPath = dir.filePath("bw.png");
  BOOST_REQUIRE(!saveColorPageScan(colorPath).isEmpty());
  BOOST_REQUIRE(!Tests::saveTextPageScan(bwPath, 300).imageInfo().empty());
  const QImage colorPage(colorPath);
  const QImage bwPage(bwPath);
  BOOST_REQUIRE(!colorPage.isNull());
  BOOST_REQUIRE(!bwPage.isNull());
  BOOST_REQUIRE(writeTwoPageTiff(inputDir + QStringLiteral("/book.tif"), colorPage, bwPage));

  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.colorModeDetect = true;
  options.reportFile = reportFile;
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());

  const QJsonArray pages = readReport(reportFile)["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 2);
  const QJsonObject first = pages[0].toObject();
  const QJsonObject second = pages[1].toObject();
  BOOST_CHECK_EQUAL(first["page"].toInt(), 1);
  BOOST_CHECK_EQUAL(second["page"].toInt(), 2);
  BOOST_CHECK(first["colorModeDetected"].toBool());
  BOOST_CHECK(second["colorModeDetected"].toBool());
  BOOST_CHECK(first["colorMode"].toString() != QStringLiteral("black-and-white"));
  BOOST_CHECK(second["colorMode"].toString() == QStringLiteral("black-and-white"));
}

// Only the files of the chosen formats go into the report, and only if the export succeeded.
BOOST_AUTO_TEST_CASE(test_report_lists_only_the_exported_files) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf"), reportFile),
                    CommandLineRun::SUCCESS);
  const QJsonArray exports = readReport(reportFile)["exports"].toArray();
  BOOST_REQUIRE_EQUAL(exports.size(), 1);
  BOOST_CHECK(exports[0].toString() == QDir::toNativeSeparators(dir.filePath("book.pdf")));
}

BOOST_AUTO_TEST_CASE(test_report_has_no_exports_after_a_failed_export) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");

  QString out;
  QString err;
  // The PDF can't be written where a folder of its name is, so the export fails after processing.
  BOOST_REQUIRE(QDir().mkpath(dir.filePath("book.pdf")));
  BOOST_CHECK_EQUAL(runExport(projectFile, &out, &err, dir.filePath("book"), QStringLiteral("pdf"), reportFile),
                    CommandLineRun::EXPORT_FAILED);
  BOOST_CHECK(readReport(reportFile)["exports"].toArray().isEmpty());
}

// Without detection, the report still tells the mode the page has in the project.
BOOST_AUTO_TEST_CASE(test_report_has_the_color_mode_of_the_project) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.reader);
  ProjectSession session = ProjectSession::load(*read.reader, DefaultParams());
  const PageId page(ImageId(dir.filePath("page.png")), PageId::SINGLE_PAGE);
  output::ColorParams colorParams = session.stages->outputFilter()->settings()->getParams(page).colorParams();
  colorParams.setColorMode(output::MIXED);
  session.stages->outputFilter()->settings()->setColorParams(page, colorParams);
  BOOST_REQUIRE(session.write(projectFile));

  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::MARGINS, reportFile),
                    CommandLineRun::SUCCESS);
  const QJsonArray pages = readReport(reportFile)["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 1);
  BOOST_CHECK(pages[0].toObject()["colorMode"].toString() == QStringLiteral("mixed"));
  BOOST_CHECK(!pages[0].toObject()["colorModeDetected"].toBool());
}

// All paths of the report are written the same way, natively, like the project file and the exports:
// a script that compares them, or matches a page of the report with a path it knows, needs that.
BOOST_AUTO_TEST_CASE(test_report_writes_all_paths_natively) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/notes.txt"), "not a scan"));
  const QString reportFile = dir.filePath("report.json");

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Default"),
                              false, QString(), reportFile),
                    CommandLineRun::SUCCESS);
  const QJsonObject report = readReport(reportFile);
  BOOST_REQUIRE_EQUAL(report["pages"].toArray().size(), 1);
  const QJsonObject page = report["pages"].toArray()[0].toObject();
  BOOST_CHECK(page["file"].toString() == QDir::toNativeSeparators(inputDir + QStringLiteral("/page.png")));
  BOOST_CHECK(page["outputFile"].toString() == QDir::toNativeSeparators(inputDir + QStringLiteral("/out/page.tif")));
  const QJsonArray passedOver = report["passedOver"].toArray();
  BOOST_REQUIRE_EQUAL(passedOver.size(), 1);
  BOOST_CHECK(passedOver[0].toString() == QDir::toNativeSeparators(inputDir + QStringLiteral("/notes.txt")));
}

// --profile Source is the old name of Minimal; the report names the profile that was used.
BOOST_AUTO_TEST_CASE(test_report_has_the_profile_that_was_used) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  const QString reportFile = dir.filePath("report.json");

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Source"),
                              false, QString(), reportFile),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(out.contains(QStringLiteral("Profile: Minimal")));
  BOOST_CHECK(readReport(reportFile)["run"].toObject()["profile"].toString() == QStringLiteral("Minimal"));
}

BOOST_AUTO_TEST_CASE(test_report_of_a_cancelled_run) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");

  QString out;
  QString err;
  const std::atomic<bool> cancel(true);
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, cancel,
                        CommandLineRun::Options::OUTPUT, reportFile),
                    CommandLineRun::CANCELLED);
  const QJsonObject run = readReport(reportFile)["run"].toObject();
  BOOST_CHECK_EQUAL(run["exitCode"].toInt(), CommandLineRun::CANCELLED);
  BOOST_CHECK(run["cancelled"].toBool());
}

BOOST_AUTO_TEST_CASE(test_report_cancelled_during_export) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  ProcessingOptions::Formats formats;
  formats.pdf = true;
  // Several pages so that export takes long enough for the cancel to land in it.
  std::vector<ImageFileInfo> files;
  for (int i = 0; i < 8; ++i) {
    files.push_back(Tests::saveTextPageScan(dir.filePath(QStringLiteral("page%1.png").arg(i)), 300));
    BOOST_REQUIRE(!files.back().imageInfo().empty());
  }
  const QString outDir = dir.filePath(QStringLiteral("out"));
  QDir().mkpath(outDir);
  const DefaultParams startValues;
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);
  ProcessingOptions& options = *session.stages->processingOptions();
  options.pdf.textLayer = false;
  options.formats = formats;
  const QString projectFile = dir.filePath(QStringLiteral("project.ScanTailor"));
  BOOST_REQUIRE(session.write(projectFile));
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");

  std::atomic<bool> cancel(false);
  std::thread flipper([&cancel, outDir]() {
    // Wait until processing has written its output, i.e. the run has reached the export.
    for (int i = 0; i < 2000; ++i) {
      QDir d(outDir);
      if (d.entryList({QStringLiteral("*.tif")}, QDir::Files).size() >= 8) {
        cancel.store(true);
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    cancel.store(true);
  });

  QString out;
  QString err;
  const CommandLineRun::ExitCode code = run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, cancel,
                                            CommandLineRun::Options::EXPORT, reportFile);
  flipper.join();

  BOOST_CHECK_EQUAL(code, CommandLineRun::CANCELLED);
  const QJsonObject report = readReport(reportFile);
  const QJsonObject run = report["run"].toObject();
  BOOST_CHECK_EQUAL(run["exitCode"].toInt(), CommandLineRun::CANCELLED);
  BOOST_CHECK(run["cancelled"].toBool());
}

// The project made in folder mode has the same pages, order and start values as the one the app
// makes from the same folder. The filters are left out: the CLI writes its project after processing.
BOOST_AUTO_TEST_CASE(test_folder_mode_creates_the_project_the_app_would_create) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("book");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  for (const QString& name : {QStringLiteral("9.png"), QStringLiteral("2.png"), QStringLiteral("010.png")}) {
    BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QLatin1Char('/') + name, 300).imageInfo().empty());
  }

  // What the app does after the Project Files dialog: load the files in SmartFilenameOrdering,
  // take the profile of the program and create and save the project.
  QFileInfoList fileInfos = QDir(inputDir).entryInfoList(QStringList(QStringLiteral("*.png")), QDir::Files);
  std::sort(fileInfos.begin(), fileInfos.end(), SmartFilenameOrdering());
  std::vector<ImageFileInfo> files;
  for (const QFileInfo& info : fileInfos) {
    QStringList loadErrors;
    QStringList partialReads;
    const std::optional<ImageFileInfo> loaded = ImageFileInfo::load(info, &loadErrors, &partialReads);
    BOOST_REQUIRE(loaded);
    BOOST_REQUIRE(partialReads.isEmpty());
    files.push_back(*loaded);
  }
  BOOST_REQUIRE_EQUAL(files.size(), 3);
  const DefaultParams startValues = DefaultParamsProvider::getInstance().getParams();
  const QString outDir = QDir(inputDir).absoluteFilePath(QStringLiteral("out"));
  const ProjectSession appSession = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, inputDir, startValues);
  const QString appProject = dir.filePath("app.ScanTailor");
  BOOST_REQUIRE(appSession.write(appProject));

  const QString cliProject = dir.filePath("cli.ScanTailor");
  QString out;
  QString err;
  const Tests::ScopedMaxThreads scopedThreads(1);
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, cliProject), CommandLineRun::SUCCESS);
  BOOST_CHECK_MESSAGE(err.isEmpty(), err.toStdString());
  BOOST_CHECK(out.contains(QStringLiteral("Profile: Default")));

  const ProjectReader::FileResult appRead = ProjectReader::readFile(appProject);
  const ProjectReader::FileResult cliRead = ProjectReader::readFile(cliProject);
  BOOST_REQUIRE(appRead.reader);
  BOOST_REQUIRE(cliRead.reader);
  const PageSequence appPages = appRead.reader->pages()->toPageSequence(IMAGE_VIEW);
  const PageSequence cliPages = cliRead.reader->pages()->toPageSequence(IMAGE_VIEW);
  BOOST_REQUIRE_EQUAL(appPages.numPages(), 3);
  BOOST_REQUIRE_EQUAL(cliPages.numPages(), 3);
  const QStringList expectedNames{QStringLiteral("2.png"), QStringLiteral("9.png"), QStringLiteral("010.png")};
  for (int i = 0; i < expectedNames.size(); ++i) {
    const PageInfo& appPage = appPages.pageAt(i);
    const PageInfo& cliPage = cliPages.pageAt(i);
    BOOST_CHECK(QFileInfo(appPage.imageId().filePath()).fileName() == expectedNames.at(i));
    BOOST_CHECK(QFileInfo(cliPage.imageId().filePath()).fileName() == expectedNames.at(i));
    BOOST_CHECK(appPage.metadata().size() == cliPage.metadata().size());
    BOOST_CHECK(appPage.metadata().dpi() == cliPage.metadata().dpi());
  }
  BOOST_CHECK(appRead.reader->pages()->layoutDirection() == cliRead.reader->pages()->layoutDirection());
  BOOST_CHECK(appRead.reader->outputDirectory() == cliRead.reader->outputDirectory());

  const std::unique_ptr<DefaultParams> appDefaults = appRead.reader->readDefaultParams();
  const std::unique_ptr<DefaultParams> cliDefaults = cliRead.reader->readDefaultParams();
  BOOST_REQUIRE(appDefaults);
  BOOST_REQUIRE(cliDefaults);
  auto defaultParamsXml = [](const DefaultParams& params) {
    QDomDocument doc;
    doc.appendChild(params.toXml(doc, QStringLiteral("default-params")));
    return doc.toString();
  };
  BOOST_CHECK(defaultParamsXml(*appDefaults) == defaultParamsXml(*cliDefaults));
  BOOST_CHECK(defaultParamsXml(*cliDefaults) == defaultParamsXml(startValues));

  auto processingOptionsXml = [](const ProcessingOptions& options) {
    QDomDocument doc;
    doc.appendChild(options.toXml(doc, QStringLiteral("processing-options")));
    return doc.toString();
  };
  BOOST_CHECK(processingOptionsXml(appRead.reader->readProcessingOptions(ProcessingOptions()))
              == processingOptionsXml(cliRead.reader->readProcessingOptions(ProcessingOptions())));
}

// ---- --progress-json ----

// Every line is valid JSON with schemaVersion 1 and an event; nothing else is in the stream.
BOOST_AUTO_TEST_CASE(test_progress_json_format) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);
  BOOST_CHECK(!lines.empty());
  for (const QJsonObject& line : lines) {
    BOOST_CHECK_EQUAL(line["schemaVersion"].toInt(), 1);
    BOOST_CHECK(line.contains(QStringLiteral("event")));
  }
  BOOST_CHECK(!out.isEmpty());
}

// pass-start comes first in each pass, done runs from 1 to total, and the step names of a full run.
BOOST_AUTO_TEST_CASE(test_progress_json_sequence_and_counters) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  std::vector<QString> passSteps;
  std::map<QString, int> passTotal;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("pass-start")) {
      passSteps.push_back(line["step"].toString());
      passTotal[line["step"].toString()] = line["total"].toInt();
    }
  }
  const std::vector<QString> expectedSteps{QStringLiteral("split-pages"), QStringLiteral("margins"),
                                           QStringLiteral("output")};
  BOOST_CHECK_EQUAL(passSteps.size(), expectedSteps.size());
  for (std::size_t i = 0; i < expectedSteps.size(); ++i) {
    BOOST_CHECK(passSteps[i] == expectedSteps[i]);
  }

  for (const QString& step : expectedSteps) {
    int count = 0;
    std::vector<int> dones;
    for (const QJsonObject& line : lines) {
      if (line["event"].toString() == QStringLiteral("page") && line["step"].toString() == step) {
        ++count;
        dones.push_back(line["done"].toInt());
        BOOST_CHECK_EQUAL(line["total"].toInt(), passTotal[step]);
      }
    }
    BOOST_CHECK_EQUAL(count, passTotal[step]);
    std::vector<int> expected;
    for (int i = 1; i <= passTotal[step]; ++i) {
      expected.push_back(i);
    }
    BOOST_CHECK_EQUAL(dones.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      BOOST_CHECK_EQUAL(dones[i], expected[i]);
    }
  }
}

// A split image yields page lines with subPage left and right.
BOOST_AUTO_TEST_CASE(test_progress_json_split_image) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath(QStringLiteral("book.png")), 300)};
  BOOST_REQUIRE(!files.front().imageInfo().empty());
  const QString outDir = dir.filePath(QStringLiteral("out"));
  BOOST_REQUIRE(QDir().mkpath(outDir));
  DefaultParams startValues;
  startValues.setPageSplitParams(page_split::PageSplitParams(page_split::TWO_PAGES));
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);
  const QString projectFile = dir.filePath(QStringLiteral("project.ScanTailor"));
  BOOST_REQUIRE(session.write(projectFile));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  bool hasLeft = false;
  bool hasRight = false;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page") && line.contains(QStringLiteral("subPage"))) {
      if (line["subPage"].toString() == QStringLiteral("left")) {
        hasLeft = true;
      }
      if (line["subPage"].toString() == QStringLiteral("right")) {
        hasRight = true;
      }
    }
  }
  BOOST_CHECK(hasLeft);
  BOOST_CHECK(hasRight);
}

// A broken image: failed with LOAD_FAILED in the first pass, skipped later, run-end exitCode 3.
BOOST_AUTO_TEST_CASE(test_progress_json_broken_image) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(Tests::writeFile(dir.filePath(QStringLiteral("page.png")), "not an image"));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::PAGES_FAILED);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  bool failedSeen = false;
  bool skippedSeen = false;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page")) {
      if (line["step"].toString() == QStringLiteral("split-pages")) {
        BOOST_CHECK(line["status"].toString() == QStringLiteral("failed"));
        BOOST_CHECK(line["errorKind"].toString() == QStringLiteral("LOAD_FAILED"));
        BOOST_CHECK(line.contains(QStringLiteral("reasons")));
        failedSeen = true;
      } else {
        BOOST_CHECK(line["status"].toString() == QStringLiteral("skipped"));
        skippedSeen = true;
      }
    }
  }
  BOOST_CHECK(failedSeen);
  BOOST_CHECK(skippedSeen);

  const QJsonObject last = lines.back();
  BOOST_CHECK(last["event"].toString() == QStringLiteral("run-end"));
  BOOST_CHECK_EQUAL(last["exitCode"].toInt(), CommandLineRun::PAGES_FAILED);
}

// outputFile appears only at step output, as a native path equal to the report's.
BOOST_AUTO_TEST_CASE(test_progress_json_output_file) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::OUTPUT, reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);
  const QJsonObject report = readReport(reportFile);
  const QJsonArray reportPages = report["pages"].toArray();
  BOOST_REQUIRE_EQUAL(reportPages.size(), 1);
  const QString reportOutputFile = reportPages[0].toObject()["outputFile"].toString();

  bool outputSeen = false;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page")) {
      if (line["step"].toString() == QStringLiteral("output")) {
        BOOST_CHECK(line.contains(QStringLiteral("outputFile")));
        BOOST_CHECK(line["outputFile"].toString() == reportOutputFile);
        outputSeen = true;
      } else {
        BOOST_CHECK(!line.contains(QStringLiteral("outputFile")));
      }
    }
  }
  BOOST_CHECK(outputSeen);
}

// Color mode detection gets its own pass between margins and output.
BOOST_AUTO_TEST_CASE(test_progress_json_color_mode_detect) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath(QStringLiteral("in"));
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!saveColorPageScan(inputDir + QStringLiteral("/page.png")).isEmpty());
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), true,
                              QString(), reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  std::vector<QString> passSteps;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("pass-start")) {
      passSteps.push_back(line["step"].toString());
    }
  }
  // color-mode-detect must come between margins and output
  auto it = std::find(passSteps.begin(), passSteps.end(), QStringLiteral("color-mode-detect"));
  BOOST_REQUIRE(it != passSteps.end());
  BOOST_CHECK(std::find(passSteps.begin(), it, QStringLiteral("margins")) != it);
  BOOST_CHECK(std::find(it, passSteps.end(), QStringLiteral("output")) != passSteps.end());

  const QJsonObject report = readReport(reportFile);
  const QJsonArray reportPages = report["pages"].toArray();
  BOOST_REQUIRE_EQUAL(reportPages.size(), 1);
  const QString reportColorMode = reportPages[0].toObject()["colorMode"].toString();

  bool colorModeSeen = false;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page")
        && line["step"].toString() == QStringLiteral("color-mode-detect")) {
      BOOST_CHECK(line.contains(QStringLiteral("colorMode")));
      if (line["status"].toString() == QStringLiteral("ok")) {
        BOOST_CHECK(line["colorMode"].toString() == reportColorMode);
      }
      colorModeSeen = true;
    }
  }
  BOOST_CHECK(colorModeSeen);
}

// An export run yields export lines with rising done, and run-end matches the report.
BOOST_AUTO_TEST_CASE(test_progress_json_export) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, dir.filePath(QStringLiteral("export")), QStringLiteral("pdf"),
                        std::nullopt, std::atomic<bool>(false), CommandLineRun::Options::EXPORT, reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  std::vector<int> exportDones;
  int exportTotal = 0;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("export")) {
      exportDones.push_back(line["done"].toInt());
      exportTotal = line["total"].toInt();
    }
  }
  BOOST_CHECK(!exportDones.empty());
  std::vector<int> expected;
  for (int i = 1; i <= exportTotal; ++i) {
    expected.push_back(i);
  }
  BOOST_CHECK_EQUAL(exportDones.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i) {
    BOOST_CHECK_EQUAL(exportDones[i], expected[i]);
  }

  const QJsonObject last = lines.back();
  BOOST_CHECK(last["event"].toString() == QStringLiteral("run-end"));
  BOOST_CHECK_EQUAL(last["exitCode"].toInt(), CommandLineRun::SUCCESS);

  const QJsonObject report = readReport(reportFile);
  BOOST_CHECK(last["exports"].toArray() == report["exports"].toArray());
}

// passedOver in run-end matches the report in folder mode.
BOOST_AUTO_TEST_CASE(test_progress_json_passed_over) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath(QStringLiteral("in"));
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  BOOST_REQUIRE(Tests::writeFile(inputDir + QStringLiteral("/notes.txt"), "text"));
  const QString reportFile = dir.filePath(QStringLiteral("report.json"));

  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QStringLiteral("Default"),
                              false, QString(), reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  const QJsonObject last = lines.back();
  BOOST_CHECK(last["event"].toString() == QStringLiteral("run-end"));
  const QJsonObject report = readReport(reportFile);
  BOOST_CHECK(last["passedOver"].toArray() == report["passedOver"].toArray());
  BOOST_REQUIRE_EQUAL(last["passedOver"].toArray().size(), 1);
  BOOST_CHECK(last["passedOver"].toArray()[0].toString()
              == QDir::toNativeSeparators(inputDir + QStringLiteral("/notes.txt")));
}

// A cancelled run ends with exitCode 5 and no page lines of later passes.
BOOST_AUTO_TEST_CASE(test_progress_json_cancelled) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  QString progress;
  const std::atomic<bool> cancel(true);
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, cancel,
                        CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::CANCELLED);
  const std::vector<QJsonObject> lines = parseProgress(progress);

  const QJsonObject last = lines.back();
  BOOST_CHECK(last["event"].toString() == QStringLiteral("run-end"));
  BOOST_CHECK_EQUAL(last["exitCode"].toInt(), CommandLineRun::CANCELLED);

  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page")) {
      BOOST_CHECK(line["step"].toString() != QStringLiteral("margins"));
      BOOST_CHECK(line["step"].toString() != QStringLiteral("output"));
    }
  }
}

// Exit 1 and 2 produce no JSON output.
BOOST_AUTO_TEST_CASE(test_progress_json_none_on_exit_1_and_2) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  // Exit 1: --dpi with a project file.
  {
    QString progress;
    CommandLineRun::Options options;
    options.projectFile = projectFile;
    options.dpi = Dpi(300, 300);
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    QTextStream progressStream(&progress);
    BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false), &progressStream),
                      CommandLineRun::USAGE_ERROR);
    BOOST_CHECK(progress.isEmpty());
  }

  // Exit 2: an unusable project file.
  {
    QString progress;
    BOOST_REQUIRE(Tests::writeFile(dir.filePath(QStringLiteral("broken.ScanTailor")), "<project"));
    CommandLineRun::Options options;
    options.projectFile = dir.filePath(QStringLiteral("broken.ScanTailor"));
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    QTextStream progressStream(&progress);
    BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false), &progressStream),
                      CommandLineRun::PROJECT_UNUSABLE);
    BOOST_CHECK(progress.isEmpty());
  }
}

// The same run with and without the JSON output gives the same out text.
BOOST_AUTO_TEST_CASE(test_progress_json_out_unchanged) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  QString outNull;
  QString errNull;
  const QString projectFile1 = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile1.isEmpty());
  BOOST_CHECK_EQUAL(run(projectFile1, &outNull, &errNull), CommandLineRun::SUCCESS);

  QDir(dir.filePath(QStringLiteral("out"))).removeRecursively();
  const QString projectFile2 = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile2.isEmpty());

  QString outJson;
  QString errJson;
  QString progress;
  BOOST_CHECK_EQUAL(run(projectFile2, &outJson, &errJson, std::nullopt, std::nullopt, std::nullopt,
                        std::atomic<bool>(false), CommandLineRun::Options::OUTPUT, std::nullopt, &progress),
                    CommandLineRun::SUCCESS);
  BOOST_CHECK(outNull == outJson);
}

// --profile-list: built-ins first, then readable file profiles sorted by name.
BOOST_AUTO_TEST_CASE(test_profile_list_orders_and_sources_profiles) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString profileDir = dir.filePath("profiles");
  BOOST_REQUIRE(QDir().mkpath(profileDir));
  DefaultParamsProfileManager manager(profileDir);
  DefaultParams params;
  BOOST_REQUIRE(manager.writeProfile(params, QStringLiteral("Alpha")));
  BOOST_REQUIRE(manager.writeProfile(params, QStringLiteral("Zeta")));
  BOOST_REQUIRE(manager.writeProfile(params, QStringLiteral("Default")));            // shadowed
  Tests::writeFile(profileDir + QStringLiteral("/Bad.stp"), QByteArray("not xml"));  // unreadable

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::profileList(outStream, errStream, profileDir), CommandLineRun::SUCCESS);
  BOOST_CHECK(err.isEmpty());

  const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
  BOOST_REQUIRE(doc.isObject());
  const QJsonObject root = doc.object();
  BOOST_CHECK_EQUAL(root["schemaVersion"].toInt(), 1);
  BOOST_CHECK(root.contains("profile"));

  const QJsonArray profiles = root["profiles"].toArray();
  BOOST_REQUIRE_EQUAL(profiles.size(), 4);
  BOOST_CHECK(profiles[0].toObject()["name"].toString() == QStringLiteral("Default"));
  BOOST_CHECK(profiles[0].toObject()["source"].toString() == QStringLiteral("built-in"));
  BOOST_CHECK(profiles[1].toObject()["name"].toString() == QStringLiteral("Minimal"));
  BOOST_CHECK(profiles[1].toObject()["source"].toString() == QStringLiteral("built-in"));
  BOOST_CHECK(profiles[2].toObject()["name"].toString() == QStringLiteral("Alpha"));
  BOOST_CHECK(profiles[2].toObject()["source"].toString() == QStringLiteral("file"));
  BOOST_CHECK(profiles[3].toObject()["name"].toString() == QStringLiteral("Zeta"));
  BOOST_CHECK(profiles[3].toObject()["source"].toString() == QStringLiteral("file"));
}

// --project-info: the fields of one page.
BOOST_AUTO_TEST_CASE(test_project_info_reports_pages) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::SUCCESS);
  BOOST_CHECK(err.isEmpty());

  const QJsonDocument doc = QJsonDocument::fromJson(out.toUtf8());
  BOOST_REQUIRE(doc.isObject());
  const QJsonObject root = doc.object();
  BOOST_CHECK_EQUAL(root["schemaVersion"].toInt(), 1);
  BOOST_CHECK(root["mode"].toString() == QStringLiteral("project"));
  BOOST_CHECK(root["projectFile"].toString() == QDir::toNativeSeparators(projectFile));
  BOOST_CHECK(root.contains("outputFolder"));
  const QJsonArray pages = root["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 1);
  const QJsonObject page = pages[0].toObject();
  BOOST_CHECK(page.contains("file"));
  BOOST_CHECK(page.contains("dpi"));
  BOOST_CHECK(page["dpiSource"].toString() == QStringLiteral("file"));
  BOOST_CHECK(page.contains("colorMode"));
  BOOST_CHECK(page.contains("outputFile"));
  BOOST_CHECK(!page.contains("status"));
  BOOST_CHECK(!page.contains("colorModeDetected"));
}

// --project-info: nothing is created.
BOOST_AUTO_TEST_CASE(test_project_info_creates_nothing) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());

  const QByteArray beforeProject = Tests::readFile(projectFile);
  const auto before = Tests::snapshotDirectory(dir.path());

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::SUCCESS);

  const QByteArray afterProject = Tests::readFile(projectFile);
  const auto after = Tests::snapshotDirectory(dir.path());
  BOOST_CHECK(beforeProject == afterProject);

  std::set<QString> beforePaths;
  std::set<QString> afterPaths;
  for (const auto& entry : before) {
    beforePaths.insert(entry.first);
  }
  for (const auto& entry : after) {
    afterPaths.insert(entry.first);
  }
  BOOST_CHECK(beforePaths == afterPaths);
}

// --project-info: deleted output folder stays deleted.
BOOST_AUTO_TEST_CASE(test_project_info_deleted_output_folder_stays_absent) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString outDir = dir.filePath("out");
  BOOST_REQUIRE(QDir(outDir).removeRecursively());

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::SUCCESS);

  BOOST_CHECK(!QDir(outDir).exists());

  const QJsonObject infoRoot = QJsonDocument::fromJson(out.toUtf8()).object();

  BOOST_CHECK(infoRoot["outputFolder"].toString() == QDir::toNativeSeparators(outDir));
}

// --profile-list: the profile a run without --profile takes matches its entry.
BOOST_AUTO_TEST_CASE(test_profile_list_profile_matches_run) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());

  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), false,
                              QString(), reportFile),
                    CommandLineRun::SUCCESS);

  const QJsonObject report = readReport(reportFile);
  const QString runProfile = report["run"].toObject()["profile"].toString();

  QString listOut;
  QString listErr;
  QTextStream listOutStream(&listOut);
  QTextStream listErrStream(&listErr);
  BOOST_CHECK_EQUAL(CommandLineRun::profileList(listOutStream, listErrStream), CommandLineRun::SUCCESS);
  const QJsonObject root = QJsonDocument::fromJson(listOut.toUtf8()).object();
  BOOST_CHECK(root["profile"].toString() == runProfile);
}

// --profile-list: an unreadable program profile omits "profile" but keeps "profiles".
BOOST_AUTO_TEST_CASE(test_profile_list_unreadable_program_profile) {
  const QString missingName = QStringLiteral("MissingSettingsProfile_xyz");
  ApplicationSettings::getInstance().setCurrentProfile(missingName);

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::profileList(outStream, errStream), CommandLineRun::SUCCESS);
  BOOST_CHECK(err.isEmpty());

  const QJsonObject root = QJsonDocument::fromJson(out.toUtf8()).object();
  BOOST_CHECK(!root.contains("profile"));
  BOOST_CHECK(root.contains("profiles"));
  BOOST_CHECK(!root["profiles"].toArray().isEmpty());
}

// --profile-list and --profile agree on an unreadable file in the profile folder: the list leaves
// it out, and a run with its name refuses it, while the readable file next to it works in both.
BOOST_AUTO_TEST_CASE(test_profile_list_unreadable_file_profile_rejected) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString profileFolder = dir.filePath("profiles");
  BOOST_REQUIRE(QDir().mkpath(profileFolder));
  BOOST_REQUIRE(Tests::writeFile(profileFolder + QStringLiteral("/Bad.stp"), "not a profile"));
  BOOST_REQUIRE(DefaultParamsProfileManager(profileFolder).writeProfile(DefaultParams(), QStringLiteral("Good")));

  QString listOut;
  QString listErr;
  QTextStream listOutStream(&listOut);
  QTextStream listErrStream(&listErr);
  BOOST_CHECK_EQUAL(CommandLineRun::profileList(listOutStream, listErrStream, profileFolder), CommandLineRun::SUCCESS);
  QStringList names;
  for (const QJsonValue& profile : QJsonDocument::fromJson(listOut.toUtf8()).object()["profiles"].toArray()) {
    names.append(profile.toObject()["name"].toString());
  }
  BOOST_CHECK(names.contains(QStringLiteral("Good")));
  BOOST_CHECK(!names.contains(QStringLiteral("Bad")));

  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  const auto runWithProfile = [&](const QString& profile, QString* err) {
    CommandLineRun::Options options;
    options.inputFolder = inputDir;
    options.profile = profile;
    options.profileFolder = profileFolder;
    options.lastStep = CommandLineRun::Options::FIX_ORIENTATION;
    QString out;
    QTextStream outStream(&out);
    QTextStream errStream(err);
    return CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false));
  };
  QString badErr;
  BOOST_CHECK_EQUAL(runWithProfile(QStringLiteral("Bad"), &badErr), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(badErr.contains(QStringLiteral("Bad")));
  QString goodErr;
  BOOST_CHECK_EQUAL(runWithProfile(QStringLiteral("Good"), &goodErr), CommandLineRun::SUCCESS);
}

// --project-info: a split image yields left and right pages.
BOOST_AUTO_TEST_CASE(test_project_info_reports_split_pages) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath(QStringLiteral("page.png")), 300)};
  BOOST_REQUIRE(!files.front().imageInfo().empty());
  const QString outDir = dir.filePath(QStringLiteral("out"));
  BOOST_REQUIRE(QDir().mkpath(outDir));
  DefaultParams startValues;
  startValues.setPageSplitParams(page_split::PageSplitParams(page_split::TWO_PAGES));
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);
  const QString projectFile = dir.filePath(QStringLiteral("project.ScanTailor"));
  BOOST_REQUIRE(session.write(projectFile));

  QString runOut;
  QString runErr;
  BOOST_CHECK_EQUAL(run(projectFile, &runOut, &runErr, std::nullopt, std::nullopt, std::nullopt,
                        std::atomic<bool>(false), CommandLineRun::Options::SPLIT_PAGES),
                    CommandLineRun::SUCCESS);

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::SUCCESS);
  const QJsonObject root = QJsonDocument::fromJson(out.toUtf8()).object();
  const QJsonArray pages = root["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 2);
  BOOST_CHECK(pages[0].toObject()["subPage"].toString() == QStringLiteral("left"));
  BOOST_CHECK(pages[1].toObject()["subPage"].toString() == QStringLiteral("right"));
  BOOST_CHECK(pages[0].toObject()["outputFile"].toString() != pages[1].toObject()["outputFile"].toString());
}

// --project-info: its pages agree with the report of a run.
BOOST_AUTO_TEST_CASE(test_project_info_matches_report) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ImageFileInfo> files{Tests::saveTextPageScan(dir.filePath(QStringLiteral("page.png")), 300)};
  BOOST_REQUIRE(!files.front().imageInfo().empty());
  const QString outDir = dir.filePath(QStringLiteral("out"));
  BOOST_REQUIRE(QDir().mkpath(outDir));
  DefaultParams startValues;
  startValues.setPageSplitParams(page_split::PageSplitParams(page_split::TWO_PAGES));
  const ProjectSession session = ProjectSession::createNew(
      ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()), outDir, QString(), startValues);
  const QString projectFile = dir.filePath(QStringLiteral("project.ScanTailor"));
  BOOST_REQUIRE(session.write(projectFile));

  const QString reportFile = dir.filePath("report.json");
  QString runOut;
  QString runErr;
  BOOST_CHECK_EQUAL(run(projectFile, &runOut, &runErr, std::nullopt, std::nullopt, std::nullopt,
                        std::atomic<bool>(false), CommandLineRun::Options::OUTPUT, reportFile),
                    CommandLineRun::SUCCESS);

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::SUCCESS);

  const QJsonObject report = readReport(reportFile);
  const QJsonArray reportPages = report["pages"].toArray();
  const QJsonArray infoPages = QJsonDocument::fromJson(out.toUtf8()).object()["pages"].toArray();
  BOOST_REQUIRE_EQUAL(infoPages.size(), reportPages.size());
  for (int i = 0; i < infoPages.size(); ++i) {
    const QJsonObject infoPage = infoPages[i].toObject();
    const QJsonObject reportPage = reportPages[i].toObject();
    BOOST_CHECK(infoPage["file"].toString() == reportPage["file"].toString());
    BOOST_CHECK(infoPage.contains("subPage") == reportPage.contains("subPage"));
    if (infoPage.contains("subPage")) {
      BOOST_CHECK(infoPage["subPage"].toString() == reportPage["subPage"].toString());
    }
    BOOST_CHECK(infoPage["dpi"].toObject() == reportPage["dpi"].toObject());
    BOOST_CHECK(infoPage["colorMode"].toString() == reportPage["colorMode"].toString());
    BOOST_CHECK(infoPage["outputFile"].toString() == reportPage["outputFile"].toString());
  }
}

// --project-info: Exit 2, empty out, message on err for unusable projects.
BOOST_AUTO_TEST_CASE(test_project_info_unusable_exit2) {
  // Not a project file.
  {
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString file = dir.filePath("not-a-project.txt");
    BOOST_REQUIRE(Tests::writeFile(file, "not a project"));
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(file, outStream, errStream), CommandLineRun::PROJECT_UNUSABLE);
    BOOST_CHECK(out.isEmpty());
    BOOST_CHECK(!err.isEmpty());
  }
  // Older version.
  {
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString projectFile = createProject(dir, 300);
    BOOST_REQUIRE(!projectFile.isEmpty());
    QString xml = QString::fromUtf8(Tests::readFile(projectFile));
    const QRegularExpressionMatch match = QRegularExpression("version=\"\\d+\"").match(xml);
    BOOST_REQUIRE(match.hasMatch());
    xml.replace(match.capturedStart(), match.capturedLength(), "version=\"4\"");
    BOOST_REQUIRE(Tests::writeFile(projectFile, xml.toUtf8()));
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::PROJECT_UNUSABLE);
    BOOST_CHECK(out.isEmpty());
    BOOST_CHECK(!err.isEmpty());
  }
  // Deleted image.
  {
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString projectFile = createProject(dir, 300);
    BOOST_REQUIRE(!projectFile.isEmpty());
    BOOST_REQUIRE(QFile::remove(dir.filePath("page.png")));
    QString out;
    QString err;
    QTextStream outStream(&out);
    QTextStream errStream(&err);
    BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::PROJECT_UNUSABLE);
    BOOST_CHECK(out.isEmpty());
    BOOST_CHECK(!err.isEmpty());
  }
}

// --project-info: a moved project gives Exit 2 with the relink message.
BOOST_AUTO_TEST_CASE(test_project_info_moved_project_relinks) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300, QStringList(), "elsewhere/out");
  BOOST_REQUIRE(!projectFile.isEmpty());
  BOOST_REQUIRE(QDir(dir.filePath("elsewhere")).removeRecursively());

  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::projectInfo(projectFile, outStream, errStream), CommandLineRun::PROJECT_UNUSABLE);
  BOOST_CHECK(err.contains(QStringLiteral("relink")));
}

// A run recreates a deleted output folder of a project that is still in place, and outputs into it.
BOOST_AUTO_TEST_CASE(test_run_recreates_deleted_output_folder_at_place) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString outDir = dir.filePath("out");
  BOOST_REQUIRE(QDir(outDir).removeRecursively());

  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err), CommandLineRun::SUCCESS);
  BOOST_CHECK(QDir(outDir).exists());
  BOOST_CHECK(QFile::exists(outDir + QStringLiteral("/page.tif")));
}
// Orientation detection in folder mode: certain pages get detected, an empty page stays undecided.
BOOST_AUTO_TEST_CASE(test_folder_mode_orientation_detect_pages) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPage(inputDir + QStringLiteral("/rot90.png"), 90, 300).imageInfo().empty());
  // empty page
  QImage empty(800, 1000, QImage::Format_RGB32);
  empty.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  empty.setDotsPerMeterX(dpm.horizontal());
  empty.setDotsPerMeterY(dpm.vertical());
  BOOST_REQUIRE(empty.save(inputDir + QStringLiteral("/empty.png")));
  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), false,
                              QStringLiteral("pages"), reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const QJsonArray pages = readReport(reportFile)["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 2);
  for (const QJsonValue& v : pages) {
    const QJsonObject p = v.toObject();
    const QString file = QFileInfo(p["file"].toString()).fileName();
    if (file == QStringLiteral("rot90.png")) {
      BOOST_CHECK_EQUAL(p["rotation"].toInt(), 270);
      BOOST_CHECK(p["rotationDetection"].toString() == QStringLiteral("detected"));
    } else {
      BOOST_CHECK_EQUAL(p["rotation"].toInt(), 0);
      BOOST_CHECK(p["rotationDetection"].toString() == QStringLiteral("undecided"));
    }
  }

  // The rotation is also stored in the project.
  const QString projectFile = project_files::defaultProjectFile(inputDir);
  const ProjectReader::FileResult read = ProjectReader::readFile(projectFile);
  BOOST_REQUIRE(read.reader);
  ProjectSession session = ProjectSession::load(*read.reader, DefaultParams());
  const PageId page(ImageId(inputDir + QStringLiteral("/rot90.png")), PageId::SINGLE_PAGE);
  BOOST_CHECK_EQUAL(session.stages->fixOrientationFilter()->settings()->getRotationFor(page.imageId()).toDegrees(),
                    270);
}

// Book rule: the empty page inherits the neighbors' orientation.
BOOST_AUTO_TEST_CASE(test_folder_mode_orientation_detect_book) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  for (int i = 0; i < 5; ++i) {
    BOOST_REQUIRE(!Tests::saveTextPage(inputDir + QStringLiteral("/p%1.png").arg(i), 90, 300).imageInfo().empty());
  }
  QImage empty(2100, 800, QImage::Format_RGB32);
  empty.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  empty.setDotsPerMeterX(dpm.horizontal());
  empty.setDotsPerMeterY(dpm.vertical());
  BOOST_REQUIRE(empty.save(inputDir + QStringLiteral("/empty.png")));
  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), false,
                              QStringLiteral("book"), reportFile),
                    CommandLineRun::SUCCESS);
  const QJsonArray pages = readReport(reportFile)["pages"].toArray();
  for (const QJsonValue& v : pages) {
    const QJsonObject p = v.toObject();
    const QString file = QFileInfo(p["file"].toString()).fileName();
    if (file == QStringLiteral("empty.png")) {
      BOOST_CHECK(p["rotationDetection"].toString() == QStringLiteral("book-rule"));
      BOOST_CHECK_EQUAL(p["rotation"].toInt(), 270);
    } else {
      BOOST_CHECK(p["rotationDetection"].toString() == QStringLiteral("detected"));
    }
  }
}

// Without the option: rotation is present, rotationDetection is not.
BOOST_AUTO_TEST_CASE(test_report_has_rotation_without_detection) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  BOOST_CHECK_EQUAL(run(projectFile, &out, &err, std::nullopt, std::nullopt, std::nullopt, std::atomic<bool>(false),
                        CommandLineRun::Options::MARGINS, reportFile),
                    CommandLineRun::SUCCESS);
  const QJsonArray pages = readReport(reportFile)["pages"].toArray();
  BOOST_REQUIRE_EQUAL(pages.size(), 1);
  const QJsonObject page = pages[0].toObject();
  BOOST_CHECK(page.contains("rotation"));
  BOOST_CHECK(!page.contains("rotationDetection"));
}

// Usage errors.
BOOST_AUTO_TEST_CASE(test_orientation_detect_only_folder_mode) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString projectFile = createProject(dir, 300);
  BOOST_REQUIRE(!projectFile.isEmpty());
  CommandLineRun::Options options;
  options.projectFile = projectFile;
  options.orientationDetect = QStringLiteral("pages");
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(!err.isEmpty());
}

BOOST_AUTO_TEST_CASE(test_orientation_detect_unknown_value) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPageScan(inputDir + QStringLiteral("/page.png"), 300).imageInfo().empty());
  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.orientationDetect = QStringLiteral("foo");
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, std::atomic<bool>(false)),
                    CommandLineRun::USAGE_ERROR);
  BOOST_CHECK(err.contains(QStringLiteral("foo")));
  BOOST_CHECK(!QFile::exists(project_files::defaultProjectFile(inputDir)));
}

// Progress json has orientation-detect pass.
BOOST_AUTO_TEST_CASE(test_progress_json_orientation_detect) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPage(inputDir + QStringLiteral("/rot90.png"), 90, 300).imageInfo().empty());
  QImage empty(800, 1000, QImage::Format_RGB32);
  empty.fill(Qt::white);
  const Dpm dpm(Dpi(300, 300));
  empty.setDotsPerMeterX(dpm.horizontal());
  empty.setDotsPerMeterY(dpm.vertical());
  BOOST_REQUIRE(empty.save(inputDir + QStringLiteral("/empty.png")));
  const QString reportFile = dir.filePath("report.json");
  QString out;
  QString err;
  QString progress;
  BOOST_CHECK_EQUAL(runFolder(inputDir, &out, &err, std::nullopt, std::nullopt, std::nullopt, QString(), false,
                              QStringLiteral("pages"), reportFile, &progress),
                    CommandLineRun::SUCCESS);
  const std::vector<QJsonObject> lines = parseProgress(progress);
  // orientation-detect is the first pass.
  std::vector<QString> passSteps;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("pass-start")) {
      passSteps.push_back(line["step"].toString());
    }
  }
  BOOST_REQUIRE(!passSteps.empty());
  BOOST_CHECK(passSteps.front() == QStringLiteral("orientation-detect"));
  BOOST_CHECK(std::find(passSteps.begin(), passSteps.end(), QStringLiteral("orientation-detect")) != passSteps.end());

  bool textSeen = false;
  bool emptySeen = false;
  for (const QJsonObject& line : lines) {
    if (line["event"].toString() == QStringLiteral("page")
        && line["step"].toString() == QStringLiteral("orientation-detect")) {
      if (line["status"].toString() == QStringLiteral("ok")) {
        BOOST_CHECK_EQUAL(line["rotation"].toInt(), 270);
        BOOST_CHECK(line["rotationDetection"].toString() == QStringLiteral("detected"));
        BOOST_CHECK(line.contains(QStringLiteral("done")));
        BOOST_CHECK(line.contains(QStringLiteral("total")));
        textSeen = true;
      } else if (line["status"].toString() == QStringLiteral("uncertain")) {
        BOOST_CHECK(!line.contains(QStringLiteral("rotation")));
        emptySeen = true;
      }
    }
  }
  BOOST_CHECK(textSeen);
  BOOST_CHECK(emptySeen);
}

// Cancelled during detection: nothing is rotated, exit 5, project is saved.
BOOST_AUTO_TEST_CASE(test_orientation_detect_cancelled) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString inputDir = dir.filePath("in");
  BOOST_REQUIRE(QDir().mkpath(inputDir));
  BOOST_REQUIRE(!Tests::saveTextPage(inputDir + QStringLiteral("/rot90.png"), 90, 300).imageInfo().empty());
  const QString projectFile = dir.filePath("p.ScanTailor");
  const QString reportFile = dir.filePath("report.json");
  std::atomic<bool> cancel(true);
  QString out;
  QString err;
  QTextStream outStream(&out);
  QTextStream errStream(&err);
  CommandLineRun::Options options;
  options.inputFolder = inputDir;
  options.projectFile = projectFile;
  options.orientationDetect = QStringLiteral("pages");
  options.reportFile = reportFile;
  BOOST_CHECK_EQUAL(CommandLineRun::run(options, outStream, errStream, cancel), CommandLineRun::CANCELLED);
  // Project was saved despite cancellation.
  BOOST_CHECK(QFile::exists(projectFile));
}

BOOST_AUTO_TEST_SUITE_END()
