// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoadErrors.h>
#include <Jp2Reader.h>
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
#include <cstdlib>
#include <cstring>

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

/** A palette image with three colours, as posterizing produces. */
QImage makePalette(const int width, const int height, const int dpi) {
  QImage image(width, height, QImage::Format_Indexed8);
  image.setColorTable({qRgb(255, 255, 255), qRgb(200, 0, 0), qRgb(0, 0, 160)});
  image.fill(0);
  image.setDotsPerMeterX(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  image.setDotsPerMeterY(static_cast<int>(std::lround(dpi / kMetersPerInch)));
  return image;
}

/** Writes a TIFF file losslessly, whatever compression the user has chosen. */
bool writeLosslessTiff(const QString& path, const QImage& image) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly)
         && TiffWriter::writeImage(file, image, {COMPRESSION_CCITTFAX4, COMPRESSION_LZW, 85});
}

/** Unpacks a zlib stream of FlateDecode. */
QByteArray inflate(const QByteArray& zlib, const int size) {
  // qUncompress() expects the uncompressed size in front, big endian.
  QByteArray data;
  data.append(static_cast<char>((size >> 24) & 0xFF));
  data.append(static_cast<char>((size >> 16) & 0xFF));
  data.append(static_cast<char>((size >> 8) & 0xFF));
  data.append(static_cast<char>(size & 0xFF));
  data.append(zlib);
  return qUncompress(data);
}

/** Reverses the PNG predictors of rows with a leading filter type byte. */
QByteArray unpredict(const QByteArray& filtered, const int bytesPerRow, const int bytesPerPixel, const int height) {
  QByteArray out(bytesPerRow * height, 0);
  for (int y = 0; y < height; ++y) {
    const auto* src = reinterpret_cast<const uchar*>(filtered.constData()) + y * (bytesPerRow + 1);
    auto* cur = reinterpret_cast<uchar*>(out.data()) + y * bytesPerRow;
    const uchar* up = (y > 0) ? cur - bytesPerRow : nullptr;
    for (int i = 0; i < bytesPerRow; ++i) {
      const int a = (i >= bytesPerPixel) ? cur[i - bytesPerPixel] : 0;
      const int b = up ? up[i] : 0;
      const int c = (up && (i >= bytesPerPixel)) ? up[i - bytesPerPixel] : 0;
      int prediction = 0;
      switch (src[0]) {
        case 1:
          prediction = a;
          break;
        case 2:
          prediction = b;
          break;
        case 3:
          prediction = (a + b) / 2;
          break;
        case 4: {
          const int p = a + b - c;
          const int pa = std::abs(p - a);
          const int pb = std::abs(p - b);
          const int pc = std::abs(p - c);
          prediction = ((pa <= pb) && (pa <= pc)) ? a : (pb <= pc) ? b : c;
          break;
        }
        default:
          break;
      }
      cur[i] = static_cast<uchar>(src[1 + i] + prediction);
    }
  }
  return out;
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
  options.bitonalCompression = PdfCompression::CCITT_G4;
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

BOOST_AUTO_TEST_CASE(test_raw_samples) {
  int components = 0;
  int bits = 0;
  QByteArray palette;

  // Black and white: black is 0, as in PDF's gray.
  QImage bitonal(10, 1, QImage::Format_Mono);
  bitonal.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
  bitonal.fill(0);
  bitonal.setPixel(0, 0, 1);
  QByteArray samples = PdfImageEncoder::rawSamples(bitonal, &components, &bits, &palette);
  BOOST_REQUIRE_EQUAL(samples.size(), 2);
  BOOST_CHECK_EQUAL(bits, 1);
  BOOST_CHECK(palette.isEmpty());
  BOOST_CHECK_EQUAL(static_cast<uchar>(samples[0]) & 0xC0, 0x40);

  // Three colours fit into 2 bits per pixel.
  QImage indexed = makePalette(5, 1, 300);
  const uchar indexes[] = {0, 1, 2, 1, 0};
  std::memcpy(indexed.scanLine(0), indexes, 5);
  BOOST_CHECK(PdfImageEncoder::isPalette(indexed));
  samples = PdfImageEncoder::rawSamples(indexed, &components, &bits, &palette);
  BOOST_CHECK_EQUAL(bits, 2);
  BOOST_CHECK(palette == QByteArray::fromHex("ffffffc800000000a0"));
  BOOST_REQUIRE_EQUAL(samples.size(), 2);
  BOOST_CHECK_EQUAL(static_cast<uchar>(samples[0]), 0x19);
  BOOST_CHECK_EQUAL(static_cast<uchar>(samples[1]), 0x00);

  // Gray and RGB with 8 bits.
  QImage gray(3, 1, QImage::Format_Grayscale8);
  const uchar grays[] = {10, 20, 30};
  std::memcpy(gray.scanLine(0), grays, 3);
  BOOST_CHECK(!PdfImageEncoder::isPalette(gray));
  samples = PdfImageEncoder::rawSamples(gray, &components, &bits, &palette);
  BOOST_CHECK_EQUAL(components, 1);
  BOOST_CHECK_EQUAL(bits, 8);
  BOOST_CHECK(samples == QByteArray::fromHex("0a141e"));

  QImage rgb(2, 1, QImage::Format_RGB32);
  rgb.setPixel(0, 0, qRgb(1, 2, 3));
  rgb.setPixel(1, 0, qRgb(4, 5, 6));
  samples = PdfImageEncoder::rawSamples(rgb, &components, &bits, &palette);
  BOOST_CHECK_EQUAL(components, 3);
  BOOST_CHECK(samples == QByteArray::fromHex("010203040506"));
}

BOOST_AUTO_TEST_CASE(test_deflate_round_trip) {
  // A photo-like RGB image.
  const int width = 37;
  const int height = 23;
  QByteArray samples(width * 3 * height, 0);
  for (int i = 0; i < samples.size(); ++i) {
    samples[i] = static_cast<char>((i * 7 + (i / 111) * 13) & 0xFF);
  }

  const QByteArray plain = PdfImageEncoder::deflate(samples, width, height, 3, 8, false);
  BOOST_REQUIRE(!plain.isEmpty());
  BOOST_CHECK(inflate(plain, samples.size()) == samples);

  const QByteArray predicted = PdfImageEncoder::deflate(samples, width, height, 3, 8, true);
  BOOST_REQUIRE(!predicted.isEmpty());
  const QByteArray filtered = inflate(predicted, (width * 3 + 1) * height);
  BOOST_REQUIRE_EQUAL(filtered.size(), (width * 3 + 1) * height);
  BOOST_CHECK(unpredict(filtered, width * 3, 3, height) == samples);
}

BOOST_AUTO_TEST_CASE(test_prepare_lossless_pages) {
  QTemporaryDir dir;
  PdfExportOptions options;
  PdfWriter::Page result;
  QStringList errors;

  // A posterized page keeps its palette with Deflate, the default.
  QImage posterized = makePalette(40, 30, 300);
  for (int y = 0; y < 30; ++y) {
    for (int x = 0; x < 40; ++x) {
      posterized.scanLine(y)[x] = static_cast<uchar>((x / 10) % 3);
    }
  }
  const QString paletteFile = dir.filePath("palette.tif");
  BOOST_REQUIRE(writeLosslessTiff(paletteFile, posterized));
  PdfExportPage palettePage(paletteFile, QString(), QString(), false);
  palettePage.analyze();
  BOOST_REQUIRE_EQUAL(palettePage.kind(), PdfExportPage::PALETTE);
  BOOST_REQUIRE(PdfExportJob::preparePage(palettePage, options, &result, &errors));
  BOOST_REQUIRE_EQUAL(result.images.size(), 1u);
  const PdfWriter::Image paletteImage = result.images[0];
  BOOST_CHECK(paletteImage.encoding == PdfWriter::Image::Encoding::FLATE);
  BOOST_CHECK_EQUAL(paletteImage.bitsPerComponent, 2);
  BOOST_CHECK_EQUAL(paletteImage.palette.size(), 9);
  BOOST_CHECK(!paletteImage.pngPredictors);

  // With JPEG, it's converted to RGB.
  options.paletteCompression = PdfCompression::JPEG;
  BOOST_REQUIRE(PdfExportJob::preparePage(palettePage, options, &result, &errors));
  BOOST_CHECK(result.images[0].encoding == PdfWriter::Image::Encoding::JPEG);
  BOOST_CHECK_EQUAL(result.images[0].components, 3);

  // Grayscale with Deflate uses the predictors, uncompressed it's just the samples.
  const QString grayFile = dir.filePath("gray.tif");
  BOOST_REQUIRE(writeLosslessTiff(grayFile, makeGray(40, 30, 300)));
  PdfExportPage grayPage(grayFile, QString(), QString(), false);
  grayPage.analyze();
  BOOST_REQUIRE_EQUAL(grayPage.kind(), PdfExportPage::IMAGE);
  options.colorCompression = PdfCompression::DEFLATE;
  BOOST_REQUIRE(PdfExportJob::preparePage(grayPage, options, &result, &errors));
  const PdfWriter::Image grayImage = result.images[0];
  BOOST_CHECK(grayImage.encoding == PdfWriter::Image::Encoding::FLATE);
  BOOST_CHECK(grayImage.pngPredictors);
  BOOST_CHECK_EQUAL(grayImage.components, 1);
  options.colorCompression = PdfCompression::NONE;
  BOOST_REQUIRE(PdfExportJob::preparePage(grayPage, options, &result, &errors));
  BOOST_CHECK(result.images[0].encoding == PdfWriter::Image::Encoding::RAW);
  BOOST_CHECK_EQUAL(result.images[0].data.size(), 40 * 30);

  // Black and white with Deflate and uncompressed.
  const QString bitonalFile = dir.filePath("bitonal.tif");
  BOOST_REQUIRE(writeLosslessTiff(bitonalFile, makeBitonal(40, 30, 300)));
  PdfExportPage bitonalPage(bitonalFile, QString(), QString(), false);
  bitonalPage.analyze();
  options.bitonalCompression = PdfCompression::DEFLATE;
  BOOST_REQUIRE(PdfExportJob::preparePage(bitonalPage, options, &result, &errors));
  const PdfWriter::Image bitonalImage = result.images[0];
  BOOST_CHECK(bitonalImage.encoding == PdfWriter::Image::Encoding::FLATE);
  BOOST_CHECK_EQUAL(bitonalImage.bitsPerComponent, 1);
  options.bitonalCompression = PdfCompression::NONE;
  BOOST_REQUIRE(PdfExportJob::preparePage(bitonalPage, options, &result, &errors));
  BOOST_CHECK(result.images[0].encoding == PdfWriter::Image::Encoding::RAW);
  BOOST_CHECK_EQUAL(result.images[0].data.size(), 5 * 30);

  // How the PDF describes these images.
  QBuffer buffer;
  BOOST_REQUIRE(buffer.open(QIODevice::WriteOnly));
  PdfWriter writer(buffer);
  BOOST_REQUIRE(writer.begin());
  const QSizeF pageSize(40, 30);
  BOOST_REQUIRE(writer.addPage({pageSize, {paletteImage}, {}}));
  BOOST_REQUIRE(writer.addPage({pageSize, {grayImage}, {}}));
  BOOST_REQUIRE(writer.addPage({pageSize, {bitonalImage}, {}}));
  BOOST_REQUIRE(writer.finish());
  const QByteArray pdf = buffer.data();
  checkStructure(pdf, 3);
  BOOST_CHECK(
      pdf.contains("/ColorSpace [/Indexed /DeviceRGB 2 <ffffffc800000000a0>] /BitsPerComponent 2 "
                   "/Filter /FlateDecode /Length "));
  BOOST_CHECK(
      pdf.contains("/ColorSpace /DeviceGray /BitsPerComponent 8 /Filter /FlateDecode /DecodeParms "
                   "<< /Predictor 15 /Colors 1 /BitsPerComponent 8 /Columns 40 >>"));
  BOOST_CHECK(pdf.contains("/ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /FlateDecode /Length "));
}

BOOST_AUTO_TEST_CASE(test_prepare_split_page_with_palette_picture) {
  QTemporaryDir dir;
  const QString mainFile = dir.filePath("page.tif");
  const QString fgFile = dir.filePath("fg.tif");
  const QString bgFile = dir.filePath("bg.tif");

  // A posterized picture in the middle.
  QImage background = makePalette(400, 600, 600);
  for (int y = 100; y < 300; ++y) {
    for (int x = 50; x < 350; ++x) {
      background.scanLine(y)[x] = static_cast<uchar>(1 + (x / 20) % 2);
    }
  }
  BOOST_REQUIRE(writeLosslessTiff(fgFile, makeBitonal(400, 600, 600)));
  BOOST_REQUIRE(writeLosslessTiff(bgFile, background));
  BOOST_REQUIRE(writeLosslessTiff(mainFile, background));

  PdfExportPage page(mainFile, fgFile, bgFile, true);
  page.analyze();
  BOOST_REQUIRE_EQUAL(page.kind(), PdfExportPage::MRC);
  BOOST_CHECK(page.hasPalettePicture());

  PdfExportOptions options;
  options.bitonalCompression = PdfCompression::DEFLATE;
  PdfWriter::Page result;
  QStringList errors;
  BOOST_REQUIRE(PdfExportJob::preparePage(page, options, &result, &errors));
  BOOST_REQUIRE_EQUAL(result.images.size(), 2u);

  // The picture keeps its palette, at half resolution without new colours.
  const PdfWriter::Image& picture = result.images[0];
  BOOST_CHECK(picture.encoding == PdfWriter::Image::Encoding::FLATE);
  BOOST_CHECK_EQUAL(picture.palette.size(), 9);
  BOOST_CHECK_EQUAL(picture.width, 150);
  BOOST_CHECK_EQUAL(picture.height, 100);

  // The text is a Deflate mask.
  const PdfWriter::Image& text = result.images[1];
  BOOST_CHECK(text.encoding == PdfWriter::Image::Encoding::FLATE);
  BOOST_CHECK(text.isMask);
  BOOST_CHECK_EQUAL(text.bitsPerComponent, 1);
}

BOOST_AUTO_TEST_CASE(test_jpeg2000) {
  // A photo-like image, so that the quality makes a difference.
  QImage rgb(96, 64, QImage::Format_RGB32);
  for (int y = 0; y < rgb.height(); ++y) {
    for (int x = 0; x < rgb.width(); ++x) {
      rgb.setPixel(x, y, qRgb((x * 37 + y * 11) & 255, (x * 13 ^ y * 29) & 255, (x * y) & 255));
    }
  }
  const QImage gray = rgb.convertToFormat(QImage::Format_Grayscale8);
  auto decode = [](const QByteArray& jp2) {
    QBuffer buffer;
    buffer.setData(jp2);
    buffer.open(QIODevice::ReadOnly);
    return Jp2Reader::readImage(buffer);
  };
  const QByteArray jp2Signature = QByteArray::fromHex("0000000c6a5020200d0a870a");

  // 100 is lossless.
  int components = 0;
  QByteArray data = PdfImageEncoder::encodeJpeg2000(rgb, 100, &components);
  BOOST_REQUIRE(data.startsWith(jp2Signature));
  BOOST_CHECK_EQUAL(components, 3);
  QImage decoded = decode(data);
  BOOST_REQUIRE(decoded.size() == rgb.size());
  BOOST_CHECK(decoded.convertToFormat(QImage::Format_RGB32) == rgb);

  data = PdfImageEncoder::encodeJpeg2000(gray, 100, &components);
  BOOST_REQUIRE(!data.isEmpty());
  BOOST_CHECK_EQUAL(components, 1);
  decoded = decode(data);
  BOOST_REQUIRE(decoded.size() == gray.size());
  BOOST_CHECK(decoded.convertToFormat(QImage::Format_Grayscale8) == gray);

  // Lower quality makes smaller files.
  const QByteArray low = PdfImageEncoder::encodeJpeg2000(rgb, 20, &components);
  const QByteArray high = PdfImageEncoder::encodeJpeg2000(rgb, 90, &components);
  BOOST_REQUIRE(!low.isEmpty());
  BOOST_REQUIRE(!high.isEmpty());
  BOOST_CHECK(low.size() < high.size());
  BOOST_CHECK(high.size() < PdfImageEncoder::encodeJpeg2000(rgb, 100, &components).size());

  // Tiny images work, too.
  BOOST_CHECK(!PdfImageEncoder::encodeJpeg2000(QImage(1, 1, QImage::Format_RGB32), 85, &components).isEmpty());
}

BOOST_AUTO_TEST_CASE(test_prepare_jpeg2000_page) {
  QTemporaryDir dir;
  const QString grayFile = dir.filePath("gray.tif");
  BOOST_REQUIRE(writeLosslessTiff(grayFile, makeGray(40, 30, 300)));
  PdfExportPage page(grayFile, QString(), QString(), false);
  page.analyze();

  PdfExportOptions options;
  options.colorCompression = PdfCompression::JPEG2000;
  PdfWriter::Page result;
  QStringList errors;
  BOOST_REQUIRE(PdfExportJob::preparePage(page, options, &result, &errors));
  BOOST_REQUIRE_EQUAL(result.images.size(), 1u);
  BOOST_CHECK(result.images[0].encoding == PdfWriter::Image::Encoding::JPX);
  BOOST_CHECK_EQUAL(result.images[0].components, 1);

  // JPEG 2000 needs PDF 1.5, which the catalog then states.
  QBuffer buffer;
  BOOST_REQUIRE(buffer.open(QIODevice::WriteOnly));
  PdfWriter writer(buffer);
  BOOST_REQUIRE(writer.begin());
  BOOST_REQUIRE(writer.addPage(result));
  BOOST_REQUIRE(writer.finish());
  const QByteArray pdf = buffer.data();
  checkStructure(pdf, 1);
  BOOST_CHECK(pdf.contains("/ColorSpace /DeviceGray /Filter /JPXDecode /Length "));
  BOOST_CHECK(pdf.contains("/Version /1.5 >>"));

  // Without JPEG 2000, there is no version in the catalog.
  options.colorCompression = PdfCompression::JPEG;
  BOOST_REQUIRE(PdfExportJob::preparePage(page, options, &result, &errors));
  QBuffer jpegBuffer;
  BOOST_REQUIRE(jpegBuffer.open(QIODevice::WriteOnly));
  PdfWriter jpegWriter(jpegBuffer);
  BOOST_REQUIRE(jpegWriter.begin());
  BOOST_REQUIRE(jpegWriter.addPage(result));
  BOOST_REQUIRE(jpegWriter.finish());
  BOOST_CHECK(!jpegBuffer.data().contains("/Version"));
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
