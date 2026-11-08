// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ExportJob.h>
#include <OcrTextOutputs.h>
#include <ProcessingOptions.h>
#include <TiffWriter.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace {
bool writeFile(const QString& path, const QByteArray& content) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && (file.write(content) == content.size());
}

QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

OcrPageTexts textsOfPage(const QByteArray& name) {
  OcrPageTexts texts;
  texts.text = "text of " + name + "\n";
  texts.hocr = "  <div class='ocr_page'>" + name + "</div>\n";
  texts.alto = "\t\t<Page ID=\"" + name + "\"/>\n";
  return texts;
}

/** Writes the pages with their names as texts and puts the files in place. */
bool writeOutputs(const OcrTextOutputs::Settings& settings, const QList<QByteArray>& pages, QString* error) {
  OcrTextOutputs outputs(settings);
  if (!outputs.begin(error)) {
    return false;
  }
  for (const QByteArray& page : pages) {
    if (!outputs.addPage(QString::fromLatin1(page) + QStringLiteral(".tif"), textsOfPage(page), error)) {
      return false;
    }
  }
  QStringList replaced;
  return outputs.finish(error) && outputs.commit(&replaced, error);
}

QStringList namesIn(const QString& dir) {
  return QDir(dir).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
}

QImage makePage(const QImage::Format format, const int width = 40, const int height = 30) {
  QImage image(width, height, format);
  image.fill(0);
  image.setDotsPerMeterX(11811);
  image.setDotsPerMeterY(11811);
  return image;
}

ExportPage writeBitonalPage(const QString& file) {
  BOOST_REQUIRE(TiffWriter::writeImage(file, makePage(QImage::Format_Mono)));
  ExportPage page(file, QString(), QString(), false);
  page.analyze();
  BOOST_REQUIRE_EQUAL(page.kind(), ExportPage::BITONAL);
  return page;
}

/** Text formats without Tesseract: the case of a language that can't be loaded. */
ExportOptions optionsWithMissingLanguage(const QString& dataDir) {
  ExportOptions options;
  options.ocr = true;
  options.ocrDataDir = dataDir;
  options.ocrLanguages = QStringLiteral("zzy");
  return options;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ExportFormatsTestSuite)

BOOST_AUTO_TEST_CASE(test_txt_joins_pages_with_a_form_feed_between_them) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.txtFile = dir.filePath("book.txt");
  settings.pageCount = 2;
  QString error;
  BOOST_REQUIRE(writeOutputs(settings, {"one", "two"}, &error));
  BOOST_CHECK(readFile(settings.txtFile) == QByteArray("text of one\n\ftext of two\n"));
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"book.txt"});
}

BOOST_AUTO_TEST_CASE(test_empty_page_in_txt_keeps_its_form_feed) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.txtFile = dir.filePath("book.txt");
  settings.pageCount = 3;
  OcrTextOutputs outputs(settings);
  QString error;
  BOOST_REQUIRE(outputs.begin(&error));
  OcrPageTexts page;
  page.text = "a\n";
  BOOST_REQUIRE(outputs.addPage("1.tif", page, &error));
  BOOST_REQUIRE(outputs.addPage("2.tif", OcrPageTexts(), &error));
  BOOST_REQUIRE(outputs.addPage("3.tif", page, &error));
  QStringList replaced;
  BOOST_REQUIRE(outputs.finish(&error) && outputs.commit(&replaced, &error));
  BOOST_CHECK(readFile(settings.txtFile) == QByteArray("a\n\f\fa\n"));
  BOOST_CHECK_EQUAL(replaced.size(), 1);
}

// File names on Linux and macOS may hold control characters, which XML 1.0 doesn't allow even escaped: the
// hOCR and ALTO files became invalid. Such characters, U+FFFE, U+FFFF and lone surrogates become U+FFFD.
BOOST_AUTO_TEST_CASE(test_xml_text_replaces_characters_xml_does_not_allow) {
  QString text = QStringLiteral("a");
  text += QChar(0x01);
  text += QStringLiteral("b&");
  text += QChar(0xFFFE);
  text += QStringLiteral("c");
  text += QChar(0xD800);
  text += QStringLiteral("d\t");
  text += QString::fromUtf8("\xC3\xA4\xF0\x9F\x98\x80");  // a-umlaut, an emoji (a surrogate pair)
  const QByteArray replacement("\xEF\xBF\xBD");
  BOOST_CHECK(OcrTextOutputs::xmlText(text)
              == "a" + replacement + "b&amp;" + replacement + "c" + replacement + "d\t\xC3\xA4\xF0\x9F\x98\x80");
}

BOOST_AUTO_TEST_CASE(test_hocr_has_head_pages_and_end_and_escapes_the_title) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.hocrFile = dir.filePath("A&B<C.hocr");
#ifdef Q_OS_WIN
  settings.hocrFile = dir.filePath("A&B.hocr");
#endif
  settings.tesseractVersion = QStringLiteral("5.5.3");
  settings.pageCount = 2;
  QString error;
  BOOST_REQUIRE(writeOutputs(settings, {"one", "two"}, &error));
  const QByteArray hocr = readFile(settings.hocrFile);
  BOOST_CHECK(hocr.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
  BOOST_CHECK(hocr.endsWith(" </body>\n</html>\n"));
  BOOST_CHECK(hocr.contains("content='tesseract 5.5.3'"));
#ifdef Q_OS_WIN
  BOOST_CHECK(hocr.contains("<title>A&amp;B</title>"));
#else
  BOOST_CHECK(hocr.contains("<title>A&amp;B&lt;C</title>"));
#endif
  const qsizetype first = hocr.indexOf("<div class='ocr_page'>one");
  BOOST_REQUIRE(first >= 0);
  BOOST_CHECK(hocr.indexOf("<div class='ocr_page'>two") > first);
}

BOOST_AUTO_TEST_CASE(test_alto_writes_one_complete_file_per_page) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("book_alto");
  settings.tesseractVersion = QStringLiteral("5.5.3");
  settings.pageCount = 2;
  QString error;
  BOOST_REQUIRE(writeOutputs(settings, {"one", "two"}, &error));
  BOOST_REQUIRE(namesIn(settings.altoDir) == (QStringList{"0001_one.xml", "0002_two.xml"}));
  const QByteArray second = readFile(QDir(settings.altoDir).filePath("0002_two.xml"));
  BOOST_CHECK(second.startsWith("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"));
  BOOST_CHECK(second.contains("ns-v3#"));
  BOOST_CHECK(second.contains("<fileName>two.tif</fileName>"));
  BOOST_CHECK(second.contains("<softwareVersion>5.5.3</softwareVersion>"));
  BOOST_CHECK(second.contains("<Page ID=\"two\"/>"));
  BOOST_CHECK(!second.contains("<Page ID=\"one\"/>"));
  BOOST_CHECK(second.endsWith("\t</Layout>\n</alto>\n"));
  // Nothing temporary remains beside the folder.
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"book_alto"});
}

BOOST_AUTO_TEST_CASE(test_alto_file_names_grow_with_the_export) {
  BOOST_CHECK(OcrTextOutputs::altoFileName(1, 5, "scan_017.tif") == "0001_scan_017.xml");
  BOOST_CHECK(OcrTextOutputs::altoFileName(12, 9999, "a.b.tif") == "0012_a.b.xml");
  BOOST_CHECK(OcrTextOutputs::altoFileName(1, 10000, "x.tif") == "00001_x.xml");
  BOOST_CHECK(OcrTextOutputs::altoFileName(10000, 10000, "x.tif") == "10000_x.xml");
}

BOOST_AUTO_TEST_CASE(test_names_outside_the_ansi_code_page_survive) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  const QString folder = dir.filePath(QString::fromUtf16(u"Bücher_Книга"));
  BOOST_REQUIRE(QDir().mkpath(folder));
  settings.altoDir = QDir(folder).filePath(QStringLiteral("alto"));
  settings.txtFile = QDir(folder).filePath(QString::fromUtf16(u"Книга.txt"));
  settings.pageCount = 1;
  OcrTextOutputs outputs(settings);
  QString error;
  BOOST_REQUIRE(outputs.begin(&error));
  BOOST_REQUIRE(outputs.addPage(QStringLiteral("page.tif"), OcrPageTexts(), &error));
  QStringList replaced;
  BOOST_REQUIRE(outputs.finish(&error) && outputs.commit(&replaced, &error));

  BOOST_CHECK(QFile::exists(settings.txtFile));
  BOOST_CHECK(QFile::exists(QDir(settings.altoDir).filePath("0001_page.xml")));
}

BOOST_AUTO_TEST_CASE(test_alto_file_name_is_escaped_and_keeps_its_characters) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("alto");
  settings.pageCount = 1;
  const QString image = QString::fromUtf16(u"Seite_ä_Я & Co.tif");
  OcrTextOutputs outputs(settings);
  QString error;
  BOOST_REQUIRE(outputs.begin(&error));
  BOOST_REQUIRE(outputs.addPage(image, OcrPageTexts(), &error));
  QStringList replaced;
  BOOST_REQUIRE(outputs.finish(&error) && outputs.commit(&replaced, &error));

  const QString altoFile = QString::fromUtf16(u"0001_Seite_ä_Я & Co.xml");
  BOOST_REQUIRE(namesIn(settings.altoDir) == QStringList{altoFile});
  const QByteArray alto = readFile(QDir(settings.altoDir).filePath(altoFile));
  BOOST_CHECK(alto.contains(QString::fromUtf16(u"<fileName>Seite_ä_Я &amp; Co.tif</fileName>").toUtf8()));
}

BOOST_AUTO_TEST_CASE(test_existing_alto_folder_is_replaced_completely) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("book_alto");
  settings.pageCount = 1;
  QString error;
  // A longer export before.
  BOOST_REQUIRE(writeOutputs(settings, {"old1", "old2", "old3"}, &error));
  BOOST_REQUIRE_EQUAL(namesIn(settings.altoDir).size(), 3);

  BOOST_REQUIRE(writeOutputs(settings, {"new"}, &error));
  BOOST_CHECK(namesIn(settings.altoDir) == QStringList{"0001_new.xml"});
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"book_alto"});
}

#ifndef Q_OS_WIN  // A file name can't hold a line break on Windows.
// The second export didn't recognize the ALTO file of the first one and refused the folder.
BOOST_AUTO_TEST_CASE(test_alto_file_with_a_line_break_in_its_name_is_replaced_by_a_new_export) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("alto");
  settings.pageCount = 1;
  for (int run = 0; run < 2; ++run) {
    OcrTextOutputs outputs(settings);
    QString error;
    BOOST_REQUIRE(outputs.begin(&error));
    BOOST_REQUIRE(outputs.addPage("line\nbreak.tif", OcrPageTexts(), &error));
    QStringList replaced;
    BOOST_REQUIRE_MESSAGE(outputs.finish(&error) && outputs.commit(&replaced, &error), error.toStdString());
  }
  BOOST_CHECK(namesIn(settings.altoDir) == QStringList{"0001_line\nbreak.xml"});
}
#endif

BOOST_AUTO_TEST_CASE(test_folder_with_foreign_files_is_not_touched) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("book_alto");
  settings.pageCount = 1;
  QString error;
  BOOST_REQUIRE(writeOutputs(settings, {"old"}, &error));
  const QString foreign = QDir(settings.altoDir).filePath("notes.txt");
  BOOST_REQUIRE(writeFile(foreign, "mine"));

  BOOST_CHECK(!writeOutputs(settings, {"new"}, &error));
  BOOST_CHECK(error.contains(QStringLiteral("book_alto")));
  BOOST_CHECK(namesIn(settings.altoDir) == (QStringList{"0001_old.xml", "notes.txt"}));
  BOOST_CHECK(readFile(foreign) == QByteArray("mine"));
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"book_alto"});

  // Files named like ALTO files with fewer digits aren't ours, nor are subfolders.
  BOOST_REQUIRE(QFile::remove(foreign));
  BOOST_REQUIRE(writeFile(QDir(settings.altoDir).filePath("001_a.xml"), "mine"));
  BOOST_CHECK(!writeOutputs(settings, {"new"}, &error));
  BOOST_REQUIRE(QFile::remove(QDir(settings.altoDir).filePath("001_a.xml")));
  BOOST_REQUIRE(QDir(settings.altoDir).mkdir("0002_sub.xml"));
  BOOST_CHECK(!writeOutputs(settings, {"new"}, &error));
}

BOOST_AUTO_TEST_CASE(test_file_in_place_of_the_alto_folder_is_not_touched) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.altoDir = dir.filePath("book_alto");
  settings.pageCount = 1;
  BOOST_REQUIRE(writeFile(settings.altoDir, "mine"));
  QString error;
  BOOST_CHECK(!writeOutputs(settings, {"new"}, &error));
  BOOST_CHECK(readFile(settings.altoDir) == QByteArray("mine"));
  // The message tells why, and doesn't end in a space.
  BOOST_CHECK(error.contains(QStringLiteral("file named like the folder")));
  BOOST_CHECK(!error.endsWith(' '));
}

BOOST_AUTO_TEST_CASE(test_cancel_leaves_everything_as_it_was) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.txtFile = dir.filePath("book.txt");
  settings.hocrFile = dir.filePath("book.hocr");
  settings.altoDir = dir.filePath("book_alto");
  settings.pageCount = 1;
  QString error;
  BOOST_REQUIRE(writeOutputs(settings, {"old"}, &error));
  const QByteArray oldTxt = readFile(settings.txtFile);
  const QByteArray oldHocr = readFile(settings.hocrFile);

  {
    OcrTextOutputs outputs(settings);
    BOOST_REQUIRE(outputs.begin(&error));
    BOOST_REQUIRE(outputs.addPage("new.tif", textsOfPage("new"), &error));
    outputs.cancel();
  }
  BOOST_CHECK(readFile(settings.txtFile) == oldTxt);
  BOOST_CHECK(readFile(settings.hocrFile) == oldHocr);
  BOOST_CHECK(namesIn(settings.altoDir) == QStringList{"0001_old.xml"});
  BOOST_CHECK(namesIn(dir.path()) == (QStringList{"book.hocr", "book.txt", "book_alto"}));

  // Without existing files, nothing remains at all.
  const QTemporaryDir other;
  BOOST_REQUIRE(other.isValid());
  settings.txtFile = other.filePath("book.txt");
  settings.hocrFile = other.filePath("book.hocr");
  settings.altoDir = other.filePath("book_alto");
  {
    OcrTextOutputs outputs(settings);
    BOOST_REQUIRE(outputs.begin(&error));
    outputs.cancel();
  }
  BOOST_CHECK(namesIn(other.path()).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_paths_follow_one_rule) {
  const ExportPaths paths = ExportPaths::fromBase("D:/Books/Book");
  BOOST_CHECK(paths.pdf == "D:/Books/Book.pdf");
  BOOST_CHECK(paths.txt == "D:/Books/Book.txt");
  BOOST_CHECK(paths.hocr == "D:/Books/Book.hocr");
  BOOST_CHECK(paths.altoDir == "D:/Books/Book_alto");

  const ExportPaths upper = ExportPaths::forPdfFile("D:/Books/Book.PDF");
  BOOST_CHECK(upper.pdf == "D:/Books/Book.PDF");
  BOOST_CHECK(upper.txt == "D:/Books/Book.txt");
  BOOST_CHECK(ExportPaths::forPdfFile("Book.dat").pdf == "Book.dat");
  BOOST_CHECK(ExportPaths::forPdfFile("Book.dat").txt == "Book.dat.txt");

  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book.pdf") == "Book");
  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book.HOCR") == "Book");
  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book.xml") == "Book");
  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book.txt") == "Book");
  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book.v2") == "Book.v2");
  BOOST_CHECK(ExportPaths::withoutFormatExtension("Book") == "Book");
  BOOST_CHECK(ExportPaths::nameFromInput(" Book.pdf ") == "Book");
  BOOST_CHECK(ExportPaths::nameFromInput("Book.pdf.txt") == "Book.pdf");
}

BOOST_AUTO_TEST_CASE(test_export_options_take_the_formats_of_the_project) {
  ProcessingOptions project;
  project.formats.pdf = false;
  project.formats.alto = true;
  project.pdf.textLayer = false;
  const ExportOptions options = ExportOptions::fromProject(project);
  BOOST_CHECK(options.formats == project.formats);
  BOOST_CHECK(!options.pdf.textLayer);
  // Setting up the text recognition stays up to the caller.
  BOOST_CHECK(!options.runsTextRecognition());
}

BOOST_AUTO_TEST_CASE(test_tesseract_runs_only_if_a_format_needs_it) {
  ExportOptions options;
  options.pdf.textLayer = false;
  BOOST_CHECK(!options.runsTextRecognition());
  options.ocr = true;
  BOOST_CHECK(!options.runsTextRecognition());  // Only a PDF without text layer.
  BOOST_CHECK(!options.addsTextLayer());
  options.formats.txt = true;
  BOOST_CHECK(options.runsTextRecognition());
  BOOST_CHECK(!options.addsTextLayer());
  options.formats.txt = false;
  options.pdf.textLayer = true;
  BOOST_CHECK(options.runsTextRecognition());
  BOOST_CHECK(options.addsTextLayer());
  options.formats.pdf = false;
  BOOST_CHECK(!options.runsTextRecognition());
}

BOOST_AUTO_TEST_CASE(test_export_without_a_format_fails) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p.tif"))};
  ExportOptions options;
  options.formats.pdf = false;
  ExportJob job(pages, ExportPaths::fromBase(dir.filePath("book")), options);
  job.run();
  BOOST_CHECK(!job.succeeded());
  BOOST_CHECK(!job.errors().isEmpty());
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"p.tif"});
}

BOOST_AUTO_TEST_CASE(test_text_formats_without_recognition_fail) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p.tif"))};
  ExportOptions options;
  options.formats.txt = true;
  ExportJob job(pages, ExportPaths::fromBase(dir.filePath("book")), options);
  job.run();
  BOOST_CHECK(!job.succeeded());
  BOOST_CHECK(!job.errors().isEmpty());
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"p.tif"});
}

BOOST_AUTO_TEST_CASE(test_missing_language_leaves_no_file_and_keeps_existing_ones) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QTemporaryDir dataDir;
  BOOST_REQUIRE(dataDir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p.tif"))};

  ExportOptions options = optionsWithMissingLanguage(dataDir.path());
  options.formats.pdf = false;
  options.formats.txt = true;
  options.formats.alto = true;
  ExportJob withoutPdf(pages, ExportPaths::fromBase(dir.filePath("book")), options);
  withoutPdf.run();
  BOOST_CHECK(!withoutPdf.succeeded());
  BOOST_CHECK(!withoutPdf.errors().isEmpty());
  BOOST_CHECK(namesIn(dir.path()) == QStringList{"p.tif"});

  // The same with a PDF, and with files from an earlier export.
  const ExportPaths paths = ExportPaths::fromBase(dir.filePath("book"));
  BOOST_REQUIRE(writeFile(paths.pdf, "old pdf"));
  BOOST_REQUIRE(writeFile(paths.txt, "old text"));
  options.formats.pdf = true;
  options.formats.alto = false;
  ExportJob withPdf(pages, paths, options);
  withPdf.run();
  BOOST_CHECK(!withPdf.succeeded());
  BOOST_CHECK(readFile(paths.pdf) == QByteArray("old pdf"));
  BOOST_CHECK(readFile(paths.txt) == QByteArray("old text"));
}

BOOST_AUTO_TEST_CASE(test_pdf_without_text_layer_starts_no_recognition) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QTemporaryDir dataDir;
  BOOST_REQUIRE(dataDir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p.tif"))};
  // The language can't be loaded, so starting Tesseract would make the export fail.
  ExportOptions options = optionsWithMissingLanguage(dataDir.path());
  options.pdf.textLayer = false;
  const ExportPaths paths = ExportPaths::fromBase(dir.filePath("book"));
  ExportJob job(pages, paths, options);
  job.run();
  BOOST_CHECK(job.succeeded());
  BOOST_CHECK(readFile(paths.pdf).startsWith("%PDF"));
}

// The export fails after it has written a page, so a job writing straight into the file would have changed it.
BOOST_AUTO_TEST_CASE(test_existing_file_is_replaced_only_after_success) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p1.tif")),
                                      writeBitonalPage(dir.filePath("p2.tif"))};
  BOOST_REQUIRE(QFile::remove(dir.filePath("p2.tif")));
  ExportOptions options;
  options.pdf.textLayer = false;
  const ExportPaths paths = ExportPaths::fromBase(dir.filePath("book"));
  BOOST_REQUIRE(writeFile(paths.pdf, "old pdf"));
  int pagesWritten = 0;
  {
    ExportJob job(pages, paths, options);
    QObject::connect(&job, &ExportJob::progress,
                     [&pagesWritten](const int pagesDone, int) { pagesWritten = pagesDone; });
    job.run();
    BOOST_CHECK(!job.succeeded());
    BOOST_CHECK(!job.wasCancelled());
    BOOST_CHECK(!job.errors().isEmpty());
  }
  BOOST_CHECK_EQUAL(pagesWritten, 1);
  BOOST_CHECK(readFile(paths.pdf) == QByteArray("old pdf"));
  BOOST_CHECK(namesIn(dir.path()) == (QStringList{"book.pdf", "p1.tif"}));

  // After a successful export it is replaced.
  const std::vector<ExportPage> goodPages{pages.front()};
  ExportJob job(goodPages, paths, options);
  job.run();
  BOOST_CHECK(job.succeeded());
  BOOST_CHECK(readFile(paths.pdf).startsWith("%PDF"));
}

// The text formats have no recognition here, so OcrTextOutputs stands in for the export.
BOOST_AUTO_TEST_CASE(test_existing_text_files_are_replaced_after_success) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  OcrTextOutputs::Settings settings;
  settings.txtFile = dir.filePath("book.txt");
  settings.hocrFile = dir.filePath("book.hocr");
  settings.pageCount = 1;
  BOOST_REQUIRE(writeFile(settings.txtFile, "old text"));
  BOOST_REQUIRE(writeFile(settings.hocrFile, "old hocr"));
  QString error;
  BOOST_REQUIRE_MESSAGE(writeOutputs(settings, {"new"}, &error), error.toStdString());
  BOOST_CHECK(readFile(settings.txtFile).contains("text of new"));
  BOOST_CHECK(readFile(settings.hocrFile).contains("<div class='ocr_page'>new</div>"));
  BOOST_CHECK(namesIn(dir.path()) == (QStringList{"book.hocr", "book.txt"}));
}

BOOST_AUTO_TEST_CASE(test_cancel_during_the_last_page_replaces_nothing) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const std::vector<ExportPage> pages{writeBitonalPage(dir.filePath("p1.tif")),
                                      writeBitonalPage(dir.filePath("p2.tif"))};
  ExportOptions options;
  options.pdf.textLayer = false;
  const ExportPaths paths = ExportPaths::fromBase(dir.filePath("book"));
  BOOST_REQUIRE(writeFile(paths.pdf, "old pdf"));
  ExportJob job(pages, paths, options);
  // The command line tool passes Ctrl+C on like this, after the page is written.
  QObject::connect(&job, &ExportJob::progress, [&job](const int pagesDone, const int pagesTotal) {
    if (pagesDone == pagesTotal) {
      job.cancel();
    }
  });
  job.run();
  BOOST_CHECK(!job.succeeded());
  BOOST_CHECK(job.wasCancelled());
  BOOST_CHECK(readFile(paths.pdf) == QByteArray("old pdf"));
}

BOOST_AUTO_TEST_CASE(test_without_pdf_a_page_is_loaded_but_not_encoded) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const ExportPage page = writeBitonalPage(dir.filePath("p.tif"));
  ExportOptions options;
  options.formats.pdf = false;
  PdfWriter::Page result;
  QStringList errors;
  BOOST_REQUIRE(ExportJob::preparePage(page, options, &result, &errors));
  BOOST_CHECK(result.images.empty());
  BOOST_CHECK(errors.isEmpty());
}

BOOST_AUTO_TEST_CASE(test_split_page_without_pdf_needs_no_background_but_matching_sizes) {
  const QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString main = dir.filePath("p.tif");
  const QString foreground = dir.filePath("fg.tif");
  const QString background = dir.filePath("bg.tif");
  BOOST_REQUIRE(TiffWriter::writeImage(main, makePage(QImage::Format_Grayscale8)));
  BOOST_REQUIRE(TiffWriter::writeImage(foreground, makePage(QImage::Format_Mono)));
  BOOST_REQUIRE(TiffWriter::writeImage(background, makePage(QImage::Format_Grayscale8)));
  ExportPage page(main, foreground, background, true);
  page.analyze();
  BOOST_REQUIRE_EQUAL(page.kind(), ExportPage::MRC);

  ExportOptions options;
  options.formats.pdf = false;
  PdfWriter::Page result;
  QStringList errors;

  // Only the foreground is read, as the missing background shows.
  BOOST_REQUIRE(QFile::remove(background));
  BOOST_CHECK(ExportJob::preparePage(page, options, &result, &errors));
  BOOST_CHECK(result.images.empty());

  // The text formats name the output file for the coordinates of the foreground.
  BOOST_REQUIRE(TiffWriter::writeImage(main, makePage(QImage::Format_Grayscale8, 41, 30)));
  BOOST_CHECK(!ExportJob::preparePage(page, options, &result, &errors));
  BOOST_CHECK(!errors.isEmpty());
}

BOOST_AUTO_TEST_SUITE_END()
