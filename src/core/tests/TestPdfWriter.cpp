// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoadErrors.h>
#include <PdfExportJob.h>
#include <PdfExportPage.h>
#include <PdfImageEncoder.h>
#include <PdfWriter.h>
#include <TiffReader.h>
#include <TiffWriter.h>
#include <tiffio.h>

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <cmath>

namespace {
const double kMetersPerInch = 0.0254;

QImage makeBitonal(const int width, const int height, const int dpi) {
  QImage image(width, height, QImage::Format_Mono);
  image.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
  image.fill(0);
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // A pattern with long and short runs, like text.
      if (((x / 7 + y / 5) % 3 == 0) || ((x * y) % 11 == 0)) {
        image.setPixel(x, y, 1);
      }
    }
  }
  image.setDotsPerMeterX(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  image.setDotsPerMeterY(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  return image;
}

QImage makeGray(const int width, const int height, const int dpi) {
  QImage image(width, height, QImage::Format_Grayscale8);
  image.fill(255);
  image.setDotsPerMeterX(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  image.setDotsPerMeterY(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  return image;
}

bool isBlack(const QImage& image, const int x, const int y) {
  return qGray(image.pixel(x, y)) < 128;
}

/** Wraps raw G4 data into a TIFF file, so that libtiff can decode it again. */
bool writeG4Tiff(const QString& path, const QByteArray& g4, const int width, const int height) {
  TIFF* tif = TIFFOpen(QFile::encodeName(path).constData(), "w");
  if (!tif) {
    return false;
  }
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(width));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(height));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
  TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(height));
  const bool ok = TIFFWriteRawStrip(tif, 0, const_cast<char*>(g4.constData()), g4.size()) == g4.size();
  TIFFClose(tif);
  return ok;
}

/** Checks that every object the cross-reference table points to starts there. */
void checkStructure(const QByteArray& pdf, const int expectedPages) {
  BOOST_REQUIRE(pdf.startsWith("%PDF-1.4\n"));
  BOOST_REQUIRE(pdf.endsWith("%%EOF\n"));

  const int startxrefPos = pdf.lastIndexOf("startxref\n");
  BOOST_REQUIRE(startxrefPos > 0);
  const QByteArray offsetLine = pdf.mid(startxrefPos + 10).split('\n').first();
  const int xrefPos = offsetLine.toInt();
  BOOST_REQUIRE(pdf.mid(xrefPos, 5) == "xref\n");

  const QList<QByteArray> header = pdf.mid(xrefPos + 5).split('\n').first().split(' ');
  BOOST_REQUIRE(header.size() == 2);
  const int objectCount = header[1].toInt();
  BOOST_REQUIRE(objectCount > 1);

  const int entriesPos = pdf.indexOf('\n', xrefPos + 5) + 1;
  BOOST_CHECK(pdf.mid(entriesPos, 20) == "0000000000 65535 f \n");
  for (int id = 1; id < objectCount; ++id) {
    const QByteArray entry = pdf.mid(entriesPos + id * 20, 20);
    BOOST_REQUIRE(entry.endsWith(" 00000 n \n"));
    const int offset = entry.left(10).toInt();
    const QByteArray expected = QByteArray::number(id) + " 0 obj\n";
    BOOST_CHECK_MESSAGE(pdf.mid(offset, expected.size()) == expected, "object " << id);
  }

  BOOST_CHECK(pdf.contains("/Size " + QByteArray::number(objectCount) + ' '));
  BOOST_CHECK(pdf.contains("/Count " + QByteArray::number(expectedPages) + " >>"));
  BOOST_CHECK_EQUAL(pdf.count("/Type /Page "), expectedPages);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(PdfWriterTestSuite)

BOOST_AUTO_TEST_CASE(test_format_number) {
  BOOST_CHECK(PdfWriter::formatNumber(0) == "0");
  BOOST_CHECK(PdfWriter::formatNumber(12) == "12");
  BOOST_CHECK(PdfWriter::formatNumber(286.08) == "286.08");
  BOOST_CHECK(PdfWriter::formatNumber(0.12345) == "0.123");
  BOOST_CHECK(PdfWriter::formatNumber(-0.0001) == "0");
  BOOST_CHECK(PdfWriter::formatNumber(-3.5) == "-3.5");
  BOOST_CHECK(PdfWriter::textString("A") == "<FEFF0041>");
}

BOOST_AUTO_TEST_CASE(test_g4_round_trip) {
  const QImage original = makeBitonal(333, 211, 300);
  ImageLoadErrorCapture capture;
  const QByteArray g4 = PdfImageEncoder::encodeG4(original);
  BOOST_REQUIRE(!g4.isEmpty());

  QTemporaryDir dir;
  const QString path = dir.filePath("g4.tif");
  BOOST_REQUIRE(writeG4Tiff(path, g4, original.width(), original.height()));
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  const QImage decoded = TiffReader::readImage(file);
  BOOST_REQUIRE(!decoded.isNull());
  BOOST_REQUIRE(decoded.size() == original.size());

  bool allMatch = true;
  for (int y = 0; y < original.height(); ++y) {
    for (int x = 0; x < original.width(); ++x) {
      allMatch = allMatch && (isBlack(decoded, x, y) == isBlack(original, x, y));
    }
  }
  BOOST_CHECK(allMatch);

  // The colour table decides which pixels are black, not the bit values.
  QImage inverted = original;
  inverted.invertPixels();
  inverted.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
  BOOST_CHECK(PdfImageEncoder::encodeG4(inverted) == g4);
}

BOOST_AUTO_TEST_CASE(test_jpeg) {
  int components = 0;
  QImage gray = makeGray(64, 48, 300);
  const QByteArray grayJpeg = PdfImageEncoder::encodeJpeg(gray, 75, &components);
  BOOST_REQUIRE(grayJpeg.size() > 4);
  BOOST_CHECK(grayJpeg.startsWith("\xFF\xD8"));
  BOOST_CHECK(grayJpeg.endsWith("\xFF\xD9"));
  BOOST_CHECK_EQUAL(components, 1);

  QImage color(64, 48, QImage::Format_RGB32);
  color.fill(qRgb(200, 30, 40));
  const QByteArray colorJpeg = PdfImageEncoder::encodeJpeg(color, 75, &components);
  BOOST_REQUIRE(!colorJpeg.isEmpty());
  BOOST_CHECK_EQUAL(components, 3);
}

BOOST_AUTO_TEST_CASE(test_content_rect) {
  QImage image = makeGray(100, 80, 300);
  BOOST_CHECK(PdfImageEncoder::contentRect(image).isNull());

  for (int y = 20; y < 30; ++y) {
    for (int x = 10; x < 45; ++x) {
      image.setPixel(x, y, qRgb(100, 100, 100));
    }
  }
  BOOST_CHECK(PdfImageEncoder::contentRect(image) == QRect(10, 20, 35, 10));
}

BOOST_AUTO_TEST_CASE(test_pdf_structure_and_page_size) {
  // The size of the test pages: 2384 x 4064 pixels at 600 dpi.
  const QImage page = makeBitonal(2384, 4064, 600);
  const QSizeF pageSize(2384 * 72.0 / 600, 4064 * 72.0 / 600);

  PdfWriter::Image bitonal;
  bitonal.encoding = PdfWriter::Image::Encoding::CCITT_G4;
  bitonal.data = PdfImageEncoder::encodeG4(page);
  bitonal.width = page.width();
  bitonal.height = page.height();
  bitonal.rect = QRectF(QPointF(0, 0), pageSize);
  BOOST_REQUIRE(!bitonal.data.isEmpty());

  PdfWriter::Image picture;
  picture.encoding = PdfWriter::Image::Encoding::JPEG;
  picture.data = PdfImageEncoder::encodeJpeg(makeGray(100, 50, 300), 75, &picture.components);
  picture.width = 100;
  picture.height = 50;
  picture.rect = QRectF(10, 20, 24, 12);

  PdfWriter::Image mask = bitonal;
  mask.isMask = true;

  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  PdfWriter writer(buffer);
  writer.setProducer("Test");
  BOOST_REQUIRE(writer.begin());
  BOOST_REQUIRE(writer.addPage({pageSize, {bitonal}, {}}));
  BOOST_REQUIRE(writer.addPage({pageSize, {picture, mask}, {}}));
  BOOST_REQUIRE(writer.finish());
  BOOST_CHECK_EQUAL(writer.pageCount(), 2);

  const QByteArray pdf = buffer.data();
  checkStructure(pdf, 2);
  // The same page size as in the PDF made by Acrobat from the same files.
  BOOST_CHECK(pdf.contains("/MediaBox [0 0 286.08 487.68]"));
  BOOST_CHECK(pdf.contains("/Filter /CCITTFaxDecode /DecodeParms << /K -1 /Columns 2384 /Rows 4064 >>"));
  BOOST_CHECK(pdf.contains("/ImageMask true"));
  BOOST_CHECK(pdf.contains("/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /DCTDecode"));
  BOOST_CHECK(pdf.contains("/Producer <FEFF0054006500730074>"));
}

BOOST_AUTO_TEST_CASE(test_text_layer) {
  const QSizeF pageSize(200, 300);
  PdfWriter::Image image;
  image.encoding = PdfWriter::Image::Encoding::CCITT_G4;
  const QImage bitonal = makeBitonal(100, 150, 36);
  image.data = PdfImageEncoder::encodeG4(bitonal);
  image.width = bitonal.width();
  image.height = bitonal.height();
  image.rect = QRectF(QPointF(0, 0), pageSize);

  PdfWriter::Word word;
  // "Grüße ", spelled out so the source file encoding doesn't matter.
  word.text = QString("Gr") + QChar(0x00FC) + QChar(0x00DF) + QString("e ");
  word.origin = QPointF(20, 100);
  word.width = 60;
  word.fontSize = 10;
  PdfWriter::Word second = word;
  second.text = "Ab";
  second.origin = QPointF(90, 100);

  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  PdfWriter writer(buffer);
  BOOST_REQUIRE(writer.begin());
  BOOST_REQUIRE(writer.addPage({pageSize, {image}, {word, second}}));
  BOOST_REQUIRE(writer.addPage({pageSize, {image}, {word}}));
  BOOST_REQUIRE(writer.addPage({pageSize, {image}, {}}));
  BOOST_REQUIRE(writer.finish());

  const QByteArray pdf = buffer.data();
  checkStructure(pdf, 3);
  // The font is embedded once and used by the pages with text only.
  BOOST_CHECK_EQUAL(pdf.count("/Subtype /Type0"), 1);
  BOOST_CHECK_EQUAL(pdf.count("/FontFile2 "), 1);
  BOOST_CHECK_EQUAL(pdf.count("/Font << /F0 "), 2);
  BOOST_CHECK(pdf.contains("/ToUnicode "));
  BOOST_CHECK(pdf.contains("/Length1 572 "));

  // The content streams are compressed; unpack the first page's one, written right before the page.
  const QByteArray streamDict = "/Filter /FlateDecode /Length ";
  const int contentPos = pdf.lastIndexOf(streamDict, pdf.indexOf("/Type /Page "));
  BOOST_REQUIRE(contentPos > 0);
  const int lengthStart = contentPos + static_cast<int>(streamDict.size());
  const int length = pdf.mid(lengthStart, pdf.indexOf(' ', lengthStart) - lengthStart).toInt();
  const int dataStart = pdf.indexOf("stream\n", lengthStart) + 7;
  QByteArray zlib = pdf.mid(dataStart, length);
  // qUncompress() expects the uncompressed size in front; a generous upper bound will do.
  zlib.prepend(QByteArray::fromHex("00100000"));
  const QByteArray content = qUncompress(zlib);
  BOOST_REQUIRE(!content.isEmpty());
  BOOST_CHECK(content.contains("BT\n3 Tr\n/F0 10 Tf\n"));
  // "Grüße " is 6 characters, half an em each: 30 points stretched to 60 points.
  BOOST_CHECK(content.contains("200 Tz\n1 0 0 1 20 200 Tm\n<0047007200FC00DF00650020> Tj\n"));
  BOOST_CHECK(content.contains("<00410062> Tj\nET\n"));
}

BOOST_AUTO_TEST_CASE(test_prepare_split_page) {
  QTemporaryDir dir;
  const QString mainFile = dir.filePath("page.tif");
  const QString fgFile = dir.filePath("fg.tif");
  const QString bgFile = dir.filePath("bg.tif");

  // A page with a picture in the middle and text below it.
  QImage background = makeGray(400, 600, 600);
  for (int y = 100; y < 300; ++y) {
    for (int x = 50; x < 350; ++x) {
      background.setPixel(x, y, qRgb((x + y) % 200, (x + y) % 200, (x + y) % 200));
    }
  }
  const QImage foreground = makeBitonal(400, 600, 600);
  BOOST_REQUIRE(TiffWriter::writeImage(fgFile, foreground));
  BOOST_REQUIRE(TiffWriter::writeImage(bgFile, background));
  BOOST_REQUIRE(TiffWriter::writeImage(mainFile, background));

  PdfExportPage page(mainFile, fgFile, bgFile, true);
  page.analyze();
  BOOST_REQUIRE_EQUAL(page.kind(), PdfExportPage::MRC);
  BOOST_CHECK(page.warning().isEmpty());

  PdfExportOptions options;
  options.backgroundScale = 2;
  PdfWriter::Page result;
  QStringList errors;
  BOOST_REQUIRE(PdfExportJob::preparePage(page, options, &result, &errors));
  BOOST_CHECK(std::abs(result.size.width() - 48.0) < 0.01);
  BOOST_CHECK(std::abs(result.size.height() - 72.0) < 0.01);
  BOOST_REQUIRE_EQUAL(result.images.size(), 2u);

  // Only the picture area of the background, at half resolution.
  const PdfWriter::Image& picture = result.images[0];
  BOOST_CHECK(picture.encoding == PdfWriter::Image::Encoding::JPEG);
  BOOST_CHECK_EQUAL(picture.width, 150);
  BOOST_CHECK_EQUAL(picture.height, 100);
  BOOST_CHECK(std::abs(picture.rect.x() - 50 * 72.0 / 600) < 0.01);
  BOOST_CHECK(std::abs(picture.rect.y() - 100 * 72.0 / 600) < 0.01);

  const PdfWriter::Image& text = result.images[1];
  BOOST_CHECK(text.encoding == PdfWriter::Image::Encoding::JBIG2);
  BOOST_CHECK(text.isMask);
  BOOST_CHECK_EQUAL(text.width, 400);

  // The same with G4 instead of JBIG2.
  options.jbig2 = false;
  PdfWriter::Page g4Result;
  BOOST_REQUIRE(PdfExportJob::preparePage(page, options, &g4Result, &errors));
  BOOST_REQUIRE_EQUAL(g4Result.images.size(), 2u);
  BOOST_CHECK(g4Result.images[1].encoding == PdfWriter::Image::Encoding::CCITT_G4);
  BOOST_CHECK(g4Result.images[1].isMask);

  // Without the split files, the mixed page is stored as one JPEG and gets a warning.
  QFile::remove(fgFile);
  page.analyze();
  BOOST_CHECK_EQUAL(page.kind(), PdfExportPage::IMAGE);
  BOOST_CHECK(!page.warning().isEmpty());

  // A missing output file.
  PdfExportPage missing(dir.filePath("none.tif"), fgFile, bgFile, false);
  missing.analyze();
  BOOST_CHECK_EQUAL(missing.kind(), PdfExportPage::MISSING);
}

BOOST_AUTO_TEST_CASE(test_export_job) {
  QTemporaryDir dir;
  std::vector<PdfExportPage> pages;
  for (int i = 0; i < 5; ++i) {
    const QString file = dir.filePath(QString("p%1.tif").arg(i));
    BOOST_REQUIRE(TiffWriter::writeImage(file, makeBitonal(200 + i, 300, 300)));
    pages.emplace_back(file, QString(), QString(), false);
    pages.back().analyze();
    BOOST_REQUIRE_EQUAL(pages.back().kind(), PdfExportPage::BITONAL);
  }

  PdfExportOptions options;
  options.threadCount = 3;
  const QString pdfFile = dir.filePath("out.pdf");
  PdfExportJob job(pages, pdfFile, options);
  job.run();
  BOOST_REQUIRE(job.succeeded());

  QFile file(pdfFile);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  const QByteArray pdf = file.readAll();
  checkStructure(pdf, 5);
  // The pages are written in the given order, even though they are prepared in parallel.
  int pos = 0;
  for (int i = 0; i < 5; ++i) {
    pos = pdf.indexOf("/Subtype /Image /Width " + QByteArray::number(200 + i) + ' ', pos);
    BOOST_CHECK_MESSAGE(pos >= 0, "page " << i);
  }
  // JBIG2 is the default.
  BOOST_CHECK_EQUAL(pdf.count("/Filter /JBIG2Decode"), 5);

  // A missing page makes the export fail without leaving a file behind.
  pages.emplace_back(dir.filePath("missing.tif"), QString(), QString(), false);
  pages.back().analyze();
  const QString failedFile = dir.filePath("failed.pdf");
  PdfExportJob failing(pages, failedFile, options);
  failing.run();
  BOOST_CHECK(!failing.succeeded());
  BOOST_CHECK(!failing.errors().isEmpty());
  BOOST_CHECK(!QFile::exists(failedFile));
}

BOOST_AUTO_TEST_SUITE_END()
