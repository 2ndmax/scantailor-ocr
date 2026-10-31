// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.
// F8: the PDF export of a page, by PdfExportJob::preparePage() and PdfWriter. The input:
//
//   byte 0       bits 0-1: the compression of grayscale and colour images (none, Deflate, JPEG,
//                JPEG 2000), bits 2-3: the same for palette images, bits 4-5: the compression of
//                1 bit images (none, Deflate, CCITT G4, JBIG2), bit 6: PDF/A, bit 7: split output
//   byte 1       bits 0-1: the image (1 bit, palette, grayscale, colour); with split output the
//                background, the foreground is always 1 bit
//   byte 2, 3    width and height minus 1 (1 to 160 pixels)
//   byte 4, 5    the DPI of the image (16 bit, big endian)
//   byte 6       the quality of JPEG and JPEG 2000 (1 to 100)
//   byte 7       the scale of the background of split output (a project file can hold any)
//   byte 8       the number of palette colours minus 1
//   then         the palette, 3 bytes per colour
//   then         the pixels, 1 byte (1 bit: bit 0, palette: the index, grayscale) or 3 bytes
//                (colour) each, with split output then 1 byte per pixel of the foreground (bit 0);
//                missing ones are white
//   the rest     words of the text layer, each: the length (bits 0-3), x and y on the page and
//                the width in 1/256 of the page, the font size in 1/4 point, then the characters
//                as UTF-16 code units (big endian)
//
// Seeds: corpus/pdf_export.
//
// The images are written as TIFF files as the Output step writes them (TiffWriter), into a
// temporary folder, and the page is analyzed and prepared from these files as the PDF export
// does. The text layer is added to the prepared page, as OCR would add it. The PDF is written
// into memory.
//
// Besides crashes and exceptions (other than std::bad_alloc), these count as findings, as PDF
// readers rely on them: every image the program wrote can be stored, uncompressed samples have
// the length their size gives, palette indexes lie inside the palette, Deflate data unpacks to
// the samples, images lie on the page, and the cross-reference table points at the objects.
#include <Dpi.h>
#include <Dpm.h>

#include <QBuffer>
#include <QByteArray>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QtEndian>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "FuzzSetup.h"
#include "PdfExportJob.h"
#include "PdfExportPage.h"
#include "PdfImageEncoder.h"
#include "PdfWriter.h"
#include "TiffWriter.h"

namespace {
const size_t HEADER_SIZE = 9;

const PdfCompression COLOR_COMPRESSIONS[]
    = {PdfCompression::NONE, PdfCompression::DEFLATE, PdfCompression::JPEG, PdfCompression::JPEG2000};
const PdfCompression BITONAL_COMPRESSIONS[]
    = {PdfCompression::NONE, PdfCompression::DEFLATE, PdfCompression::CCITT_G4, PdfCompression::JBIG2};

enum ImageKind { MONO, PALETTE, GRAY, COLOR };

// A broken rule: reported like a crash, so that libFuzzer keeps the input.
void check(const bool ok, const char* rule) {
  if (!ok) {
    std::fprintf(stderr, "Rule broken: %s\n", rule);
    std::abort();
  }
}

// Reads the input byte by byte; past its end it gives the default.
class Reader {
 public:
  Reader(const uint8_t* data, const size_t size) : m_data(data), m_left(size) {}

  uint8_t next(const uint8_t missing) {
    if (m_left == 0) {
      return missing;
    }
    --m_left;
    return *m_data++;
  }

  size_t left() const { return m_left; }

 private:
  const uint8_t* m_data;
  size_t m_left;
};

QImage buildImage(const ImageKind kind, const QSize& size, const QVector<QRgb>& palette, Reader& in) {
  QImage image;
  switch (kind) {
    case MONO:
      image = QImage(size, QImage::Format_Mono);
      image.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
      break;
    case PALETTE:
      image = QImage(size, QImage::Format_Indexed8);
      image.setColorTable(palette);
      break;
    case GRAY:
      image = QImage(size, QImage::Format_Grayscale8);
      break;
    case COLOR:
      image = QImage(size, QImage::Format_RGB32);
      break;
  }
  for (int y = 0; y < size.height(); ++y) {
    for (int x = 0; x < size.width(); ++x) {
      switch (kind) {
        case MONO:
          image.setPixel(x, y, in.next(0) & 1);
          break;
        case PALETTE:
          image.setPixel(x, y, in.next(0) % palette.size());
          break;
        case GRAY:
          image.scanLine(y)[x] = in.next(255);
          break;
        case COLOR: {
          const uint8_t r = in.next(255);
          const uint8_t g = in.next(255);
          const uint8_t b = in.next(255);
          reinterpret_cast<QRgb*>(image.scanLine(y))[x] = qRgb(r, g, b);
          break;
        }
      }
    }
  }
  return image;
}

void writeTiff(const QString& path, const QImage& image) {
  check(TiffWriter::writeImage(path, image), "the TIFF file of the page can be written");
}

// The bytes of one row of samples.
qsizetype rowBytes(const PdfWriter::Image& image) {
  return (static_cast<qsizetype>(image.width) * image.components * image.bitsPerComponent + 7) / 8;
}

void checkPaletteIndexes(const PdfWriter::Image& image, const QByteArray& samples) {
  const int colorCount = static_cast<int>(image.palette.size() / 3);
  check((image.palette.size() % 3 == 0) && (colorCount >= 1) && (colorCount <= 256), "a palette has 1 to 256 colours");
  const int bits = image.bitsPerComponent;
  for (int y = 0; y < image.height; ++y) {
    const auto* row = reinterpret_cast<const uint8_t*>(samples.constData()) + y * rowBytes(image);
    for (int x = 0; x < image.width; ++x) {
      const int bit = x * bits;
      const int index = (row[bit / 8] >> (8 - bits - bit % 8)) & ((1 << bits) - 1);
      check(index < colorCount, "palette indexes lie inside the palette");
    }
  }
}

void checkImage(const PdfWriter::Image& image, const QSizeF& pageSize) {
  check(image.width > 0 && image.height > 0 && !image.data.isEmpty(), "an image has a size and data");
  const QRectF page(QPointF(0, 0), pageSize);
  // Rounding of the background part may leave it a tiny bit outside.
  check(page.adjusted(-0.01, -0.01, 0.01, 0.01).contains(image.rect), "images lie on the page");

  using Encoding = PdfWriter::Image::Encoding;
  if ((image.encoding != Encoding::RAW) && (image.encoding != Encoding::FLATE)) {
    return;
  }
  qsizetype expected = rowBytes(image) * image.height;
  QByteArray samples = image.data;
  if (image.encoding == Encoding::FLATE) {
    if (image.pngPredictors) {
      // Each row starts with the byte that says which predictor it uses.
      expected += image.height;
    }
    // qUncompress() takes zlib data with the expected size in front.
    QByteArray sized(4, '\0');
    qToBigEndian(static_cast<quint32>(expected), sized.data());
    samples = qUncompress(sized + image.data);
  }
  check(samples.size() == expected, "the samples have the length the image size gives");
  if (!image.palette.isEmpty() && !image.pngPredictors) {
    checkPaletteIndexes(image, samples);
  }
}

void checkCrossReferences(const QByteArray& pdf) {
  const qsizetype startXref = pdf.lastIndexOf("startxref\n");
  check(startXref >= 0, "the PDF has a startxref");
  const qsizetype xrefOffset = pdf.mid(startXref + 10, 20).split('\n').value(0).toLongLong();
  check(pdf.mid(xrefOffset, 7) == "xref\n0 ", "startxref points at the cross-reference table");
  const qsizetype countEnd = pdf.indexOf('\n', xrefOffset + 7);
  const int objectCount = pdf.mid(xrefOffset + 7, countEnd - xrefOffset - 7).toInt();
  check(objectCount >= 1, "the cross-reference table has entries");
  const qsizetype entries = countEnd + 1;
  check(pdf.size() >= entries + objectCount * 20, "the cross-reference table is complete");
  for (int id = 1; id < objectCount; ++id) {
    const QByteArray entry = pdf.mid(entries + id * 20, 20);
    check(entry.endsWith(" 00000 n \n"), "a cross-reference entry has 20 bytes");
    const qsizetype offset = entry.left(10).toLongLong();
    const QByteArray header = QByteArray::number(id) + " 0 obj\n";
    check(pdf.mid(offset, header.size()) == header, "a cross-reference entry points at its object");
  }
}

void addWords(Reader& in, PdfWriter::Page* page) {
  while (in.left() >= 5) {
    PdfWriter::Word word;
    const int length = in.next(0) & 15;
    const double x = in.next(0) * page->size.width() / 256;
    const double y = in.next(0) * page->size.height() / 256;
    word.origin = QPointF(x, y);
    word.width = in.next(0) * page->size.width() / 256;
    word.fontSize = in.next(0) / 4.0;
    for (int i = 0; i < length; ++i) {
      const uint8_t high = in.next(0);
      const uint8_t low = in.next(0);
      word.text += QChar(static_cast<char16_t>((high << 8) | low));
    }
    page->words.push_back(word);
  }
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  if (size < HEADER_SIZE) {
    return 0;
  }
  // Lives until the process ends; the files in it are written anew by each run.
  static const QTemporaryDir* const dir = new QTemporaryDir();
  const QString mainFile = dir->filePath("page.tif");
  const QString foregroundFile = dir->filePath("foreground/page.tif");
  const QString backgroundFile = dir->filePath("background/page.tif");
  QDir().mkpath(dir->filePath("foreground"));
  QDir().mkpath(dir->filePath("background"));
  QFile::remove(foregroundFile);
  QFile::remove(backgroundFile);

  PdfExportOptions options;
  options.colorCompression = COLOR_COMPRESSIONS[data[0] & 3];
  options.paletteCompression = COLOR_COMPRESSIONS[(data[0] >> 2) & 3];
  options.bitonalCompression = BITONAL_COMPRESSIONS[(data[0] >> 4) & 3];
  options.pdfA = (data[0] & 64) != 0;
  const bool splitOutput = (data[0] & 128) != 0;
  const auto kind = static_cast<ImageKind>(data[1] & 3);
  const QSize imageSize(data[2] % 160 + 1, data[3] % 160 + 1);
  const int dpi = (data[4] << 8) | data[5];
  options.colorQuality = data[6] % 100 + 1;
  options.paletteQuality = options.colorQuality;
  options.backgroundScale = data[7];
  const int colorCount = data[8] + 1;

  Reader in(data + HEADER_SIZE, size - HEADER_SIZE);
  QVector<QRgb> palette;
  for (int i = 0; i < colorCount; ++i) {
    const uint8_t r = in.next(255);
    const uint8_t g = in.next(255);
    const uint8_t b = in.next(255);
    palette.push_back(qRgb(r, g, b));
  }
  QImage image = buildImage(kind, imageSize, palette, in);
  const Dpm dpm(Dpi(dpi, dpi));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  writeTiff(mainFile, image);
  if (splitOutput) {
    QImage foreground = buildImage(MONO, imageSize, palette, in);
    foreground.setDotsPerMeterX(dpm.horizontal());
    foreground.setDotsPerMeterY(dpm.vertical());
    writeTiff(foregroundFile, foreground);
    writeTiff(backgroundFile, image);
  }

  PdfExportPage page(mainFile, foregroundFile, backgroundFile, splitOutput);
  page.analyze();
  PdfWriter::Page pdfPage;
  QStringList errors;
  try {
    check(PdfExportJob::preparePage(page, options, &pdfPage, &errors, nullptr, nullptr),
          "every image the program wrote can be stored");
    for (const PdfWriter::Image& pdfImage : pdfPage.images) {
      checkImage(pdfImage, pdfPage.size);
    }
    addWords(in, &pdfPage);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);
    PdfWriter writer(buffer);
    writer.setProducer(QStringLiteral("ScanTailor OCR"));
    writer.setPdfA(options.pdfA);
    check(writer.begin() && writer.addPage(pdfPage) && writer.finish(), "the PDF can be written");
    checkCrossReferences(buffer.data());
  } catch (const std::bad_alloc&) {
    // The program hands it to its out-of-memory handler.
  }
  return 0;
}
