// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoadErrors.h>
#include <PdfImageEncoder.h>
#include <PdfWriter.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace {
/*
 * A decoder for what PdfImageEncoder::encodeJbig2() writes, written from the
 * JBIG2 standard (ITU-T T.88) for these tests only: the MQ decoder of annex E
 * and generic region decoding with template 0 (6.2.5).  It shares no code with
 * the encoder, so that a misunderstanding on one side shows up as a mismatch.
 */

struct QeEntry {
  uint16_t qe;
  uint8_t nmps;
  uint8_t nlps;
  uint8_t switchMps;
};

// Table E.1.
const QeEntry kQeTable[47]
    = {{0x5601, 1, 1, 1},   {0x3401, 2, 6, 0},   {0x1801, 3, 9, 0},   {0x0AC1, 4, 12, 0},  {0x0521, 5, 29, 0},
       {0x0221, 38, 33, 0}, {0x5601, 7, 6, 1},   {0x5401, 8, 14, 0},  {0x4801, 9, 14, 0},  {0x3801, 10, 14, 0},
       {0x3001, 11, 17, 0}, {0x2401, 12, 18, 0}, {0x1C01, 13, 20, 0}, {0x1601, 29, 21, 0}, {0x5601, 15, 14, 1},
       {0x5401, 16, 14, 0}, {0x5101, 17, 15, 0}, {0x4801, 18, 16, 0}, {0x3801, 19, 17, 0}, {0x3401, 20, 18, 0},
       {0x3001, 21, 19, 0}, {0x2801, 22, 19, 0}, {0x2401, 23, 20, 0}, {0x2201, 24, 21, 0}, {0x1C01, 25, 22, 0},
       {0x1801, 26, 23, 0}, {0x1601, 27, 24, 0}, {0x1401, 28, 25, 0}, {0x1201, 29, 26, 0}, {0x1101, 30, 27, 0},
       {0x0AC1, 31, 28, 0}, {0x09C1, 32, 29, 0}, {0x08A1, 33, 30, 0}, {0x0521, 34, 31, 0}, {0x0441, 35, 32, 0},
       {0x02A1, 36, 33, 0}, {0x0221, 37, 34, 0}, {0x0141, 38, 35, 0}, {0x0111, 39, 36, 0}, {0x0085, 40, 37, 0},
       {0x0049, 41, 38, 0}, {0x0025, 42, 39, 0}, {0x0015, 43, 40, 0}, {0x0009, 44, 41, 0}, {0x0005, 45, 42, 0},
       {0x0001, 45, 43, 0}, {0x5601, 46, 46, 0}};

class MqDecoder {
 public:
  MqDecoder(const uint8_t* data, const size_t size)
      : m_data(data), m_size(size), m_index(1 << 16, 0), m_mps(1 << 16, 0) {
    // INITDEC (E.3.5), with the C register stored inverted as in the standard's flow charts.
    m_c = (static_cast<uint32_t>(byteAt(0)) ^ 0xFF) << 16;
    byteIn();
    m_c <<= 7;
    m_ct -= 7;
    m_a = 0x8000;
  }

  // DECODE (E.3.2) with MPS_EXCHANGE, LPS_EXCHANGE and RENORMD.
  int decode(const uint32_t cx) {
    const QeEntry& entry = kQeTable[m_index[cx]];
    int d;
    m_a = static_cast<uint32_t>(m_a - entry.qe);
    if ((m_c >> 16) < m_a) {
      if ((m_a & 0x8000) != 0) {
        return m_mps[cx];
      }
      if (m_a < entry.qe) {
        d = 1 - m_mps[cx];
        if (entry.switchMps) {
          m_mps[cx] = static_cast<uint8_t>(1 - m_mps[cx]);
        }
        m_index[cx] = entry.nlps;
      } else {
        d = m_mps[cx];
        m_index[cx] = entry.nmps;
      }
    } else {
      m_c -= m_a << 16;
      if (m_a < entry.qe) {
        m_a = entry.qe;
        d = m_mps[cx];
        m_index[cx] = entry.nmps;
      } else {
        m_a = entry.qe;
        d = 1 - m_mps[cx];
        if (entry.switchMps) {
          m_mps[cx] = static_cast<uint8_t>(1 - m_mps[cx]);
        }
        m_index[cx] = entry.nlps;
      }
    }
    do {
      if (m_ct == 0) {
        byteIn();
      }
      m_a <<= 1;
      m_c <<= 1;
      --m_ct;
    } while ((m_a & 0x8000) == 0);
    return d;
  }

 private:
  // Past the end, the data reads as a marker, which feeds 1 bits.
  uint8_t byteAt(const size_t i) const { return (i < m_size) ? m_data[i] : 0xFF; }

  // BYTEIN (E.3.4).
  void byteIn() {
    if (byteAt(m_bp) == 0xFF) {
      if (byteAt(m_bp + 1) > 0x8F) {
        m_ct = 8;
      } else {
        ++m_bp;
        m_c += 0xFE00 - (static_cast<uint32_t>(byteAt(m_bp)) << 9);
        m_ct = 7;
      }
    } else {
      ++m_bp;
      m_c += 0xFF00 - (static_cast<uint32_t>(byteAt(m_bp)) << 8);
      m_ct = 8;
    }
  }

  const uint8_t* m_data;
  size_t m_size;
  size_t m_bp = 0;
  uint32_t m_a = 0;
  uint32_t m_c = 0;
  int m_ct = 0;
  std::vector<uint8_t> m_index;
  std::vector<uint8_t> m_mps;
};

uint32_t readU32(const QByteArray& data, const int pos) {
  return (static_cast<uint32_t>(static_cast<uint8_t>(data[pos])) << 24)
         | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 1])) << 16)
         | (static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 2])) << 8)
         | static_cast<uint32_t>(static_cast<uint8_t>(data[pos + 3]));
}

/** Decodes the output of encodeJbig2() into rows of 0 (white) and 1 (black). */
bool decodeJbig2(const QByteArray& data, int* width, int* height, std::vector<uint8_t>* pixels) {
  // Page information segment: number 0, type 48, no references, page 1, 19 bytes.
  if ((data.size() < 11 + 19 + 11 + 26) || (readU32(data, 0) != 0) || (data[4] != 48) || (data[5] != 0)
      || (data[6] != 1) || (readU32(data, 7) != 19)) {
    BOOST_TEST_MESSAGE("bad page information segment header");
    return false;
  }
  const int w = static_cast<int>(readU32(data, 11));
  const int h = static_cast<int>(readU32(data, 15));
  if ((data[27] & 0x01) == 0) {
    BOOST_TEST_MESSAGE("page not marked lossless");
    return false;
  }

  // Immediate generic region segment: number 1, type 38, no references, page 1.
  int pos = 11 + 19;
  if ((readU32(data, pos) != 1) || (data[pos + 4] != 38) || (data[pos + 5] != 0) || (data[pos + 6] != 1)) {
    BOOST_TEST_MESSAGE("bad generic region segment header");
    return false;
  }
  const uint32_t length = readU32(data, pos + 7);
  pos += 11;
  if (static_cast<int>(length) != data.size() - pos) {
    BOOST_TEST_MESSAGE("wrong segment data length");
    return false;
  }
  if ((static_cast<int>(readU32(data, pos)) != w) || (static_cast<int>(readU32(data, pos + 4)) != h)
      || (readU32(data, pos + 8) != 0) || (readU32(data, pos + 12) != 0)) {
    BOOST_TEST_MESSAGE("region doesn't cover the page");
    return false;
  }
  const uint8_t flags = static_cast<uint8_t>(data[pos + 17]);
  if ((flags & 0x07) != 0) {
    BOOST_TEST_MESSAGE("not arithmetic coding with template 0");
    return false;
  }
  const bool tpgdon = (flags & 0x08) != 0;
  int atx[4];
  int aty[4];
  for (int i = 0; i < 4; ++i) {
    atx[i] = static_cast<signed char>(data[pos + 18 + 2 * i]);
    aty[i] = static_cast<signed char>(data[pos + 19 + 2 * i]);
  }
  pos += 26;

  MqDecoder decoder(reinterpret_cast<const uint8_t*>(data.constData()) + pos, static_cast<size_t>(data.size() - pos));
  pixels->assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0);
  const auto get = [&](const int x, const int y) -> uint32_t {
    if ((x < 0) || (x >= w) || (y < 0)) {
      return 0;
    }
    return (*pixels)[static_cast<size_t>(y) * w + x];
  };

  // 6.2.5.7, figure 3: the template 0 context.
  int ltp = 0;
  for (int y = 0; y < h; ++y) {
    if (tpgdon) {
      ltp ^= decoder.decode(0x9B25);
      if (ltp) {
        for (int x = 0; x < w; ++x) {
          (*pixels)[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(get(x, y - 1));
        }
        continue;
      }
    }
    for (int x = 0; x < w; ++x) {
      uint32_t cx = get(x - 1, y) | (get(x - 2, y) << 1) | (get(x - 3, y) << 2) | (get(x - 4, y) << 3);
      cx |= get(x + atx[0], y + aty[0]) << 4;
      cx |= (get(x + 2, y - 1) << 5) | (get(x + 1, y - 1) << 6) | (get(x, y - 1) << 7) | (get(x - 1, y - 1) << 8)
            | (get(x - 2, y - 1) << 9);
      cx |= get(x + atx[1], y + aty[1]) << 10;
      cx |= get(x + atx[2], y + aty[2]) << 11;
      cx |= (get(x + 1, y - 2) << 12) | (get(x, y - 2) << 13) | (get(x - 1, y - 2) << 14);
      cx |= get(x + atx[3], y + aty[3]) << 15;
      (*pixels)[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(decoder.decode(cx));
    }
  }
  *width = w;
  *height = h;
  return true;
}  // decodeJbig2

bool isBlack(const QImage& image, const int x, const int y) {
  return qGray(image.pixel(x, y)) < 128;
}

QImage makeMono(const int width, const int height) {
  QImage image(width, height, QImage::Format_Mono);
  image.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
  image.fill(0);
  return image;
}

/** Text-like strokes, some repeated rows (typical prediction) and random noise. */
QImage makePattern(const int width, const int height, uint32_t seed) {
  QImage image = makeMono(width, height);
  for (int y = 0; y < height; ++y) {
    const bool blankBand = (y / 9) % 4 == 3;
    for (int x = 0; x < width; ++x) {
      seed = seed * 1664525u + 1013904223u;
      bool black = !blankBand && (((x / 5 + y / 3) % 4 == 0) || ((x * 7 + y * 3) % 23 == 0));
      if ((seed >> 24) < 6) {
        black = !black;
      }
      if (black) {
        image.setPixel(x, y, 1);
      }
    }
  }
  return image;
}

/** Number of pixels whose colour differs; -1 if the sizes differ. */
long countMismatches(const QImage& original, const int width, const int height, const std::vector<uint8_t>& pixels) {
  if ((original.width() != width) || (original.height() != height)) {
    return -1;
  }
  long mismatches = 0;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (isBlack(original, x, y) != (pixels[static_cast<size_t>(y) * width + x] != 0)) {
        ++mismatches;
      }
    }
  }
  return mismatches;
}

void checkRoundTrip(const QImage& original) {
  ImageLoadErrorCapture capture;
  const QByteArray encoded = PdfImageEncoder::encodeJbig2(original);
  BOOST_REQUIRE(!encoded.isEmpty());
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;
  BOOST_REQUIRE(decodeJbig2(encoded, &width, &height, &pixels));
  BOOST_CHECK_MESSAGE(countMismatches(original, width, height, pixels) == 0,
                      "round trip of " << original.width() << " x " << original.height());
}

/** A PDF of 72 dpi images, so that one image pixel is one point. */
QByteArray makePdf(const std::vector<PdfWriter::Image>& pageImages) {
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  PdfWriter writer(buffer);
  if (!writer.begin()) {
    return QByteArray();
  }
  for (const PdfWriter::Image& image : pageImages) {
    if (!writer.addPage({QSizeF(image.width, image.height), {image}, {}})) {
      return QByteArray();
    }
  }
  if (!writer.finish()) {
    return QByteArray();
  }
  return buffer.data();
}

PdfWriter::Image makePdfImage(const QImage& image, const PdfWriter::Image::Encoding encoding, const bool isMask) {
  PdfWriter::Image result;
  result.encoding = encoding;
  result.data = (encoding == PdfWriter::Image::Encoding::JBIG2) ? PdfImageEncoder::encodeJbig2(image)
                                                                : PdfImageEncoder::encodeG4(image);
  result.width = image.width();
  result.height = image.height();
  result.isMask = isMask;
  result.rect = QRectF(0, 0, image.width(), image.height());
  return result;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(Jbig2TestSuite)

BOOST_AUTO_TEST_CASE(test_jbig2_round_trip) {
  // Widths around the 32 bit words the encoder works with.
  for (const int width : {1, 2, 7, 8, 9, 31, 32, 33, 63, 64, 65, 100}) {
    checkRoundTrip(makePattern(width, 13, 12345u + width));
  }
  checkRoundTrip(makePattern(333, 211, 1u));
  checkRoundTrip(makePattern(1, 1, 2u));

  // Uniform images: everything is predicted, very little data.
  QImage white = makeMono(150, 80);
  checkRoundTrip(white);
  QImage black = makeMono(70, 40);
  black.fill(1);
  checkRoundTrip(black);

  // Not 1 bit: converted, dark pixels black.
  QImage gray(40, 30, QImage::Format_Grayscale8);
  gray.fill(255);
  for (int y = 5; y < 20; ++y) {
    for (int x = 3; x < 17; ++x) {
      gray.setPixel(x, y, qRgb(20, 20, 20));
    }
  }
  checkRoundTrip(gray);
}

BOOST_AUTO_TEST_CASE(test_jbig2_colour_table) {
  // The colour table decides which pixels are black, not the bit values.
  const QImage original = makePattern(97, 41, 7u);
  QImage inverted = original;
  inverted.invertPixels();
  inverted.setColorTable({qRgb(0, 0, 0), qRgb(255, 255, 255)});
  BOOST_CHECK(PdfImageEncoder::encodeJbig2(inverted) == PdfImageEncoder::encodeJbig2(original));
}

BOOST_AUTO_TEST_CASE(test_jbig2_in_pdf) {
  const QImage image = makePattern(120, 90, 3u);
  const QByteArray pdf = makePdf({makePdfImage(image, PdfWriter::Image::Encoding::JBIG2, false),
                                  makePdfImage(image, PdfWriter::Image::Encoding::JBIG2, true)});
  BOOST_REQUIRE(!pdf.isEmpty());
  BOOST_CHECK(pdf.contains("/ColorSpace /DeviceGray /BitsPerComponent 1 /Filter /JBIG2Decode"));
  BOOST_CHECK(pdf.contains("/ImageMask true /BitsPerComponent 1 /Filter /JBIG2Decode"));
  BOOST_CHECK(!pdf.contains("/JBIG2Globals"));
}

/*
 * Independent check: Poppler (the PDF library of Okular, Evince and others) renders
 * a PDF with JBIG2 and G4 pages, and the result has to match the original pixels.
 * Skipped if Poppler's pdftoppm isn't installed (CI installs it on Linux).
 */
BOOST_AUTO_TEST_CASE(test_jbig2_rendered_by_poppler) {
  const QString pdftoppm = QStandardPaths::findExecutable("pdftoppm");
  if (pdftoppm.isEmpty()) {
    BOOST_TEST_MESSAGE("pdftoppm not found, skipping the check with Poppler.");
    return;
  }

  // QProcess needs an application object.
  std::unique_ptr<QCoreApplication> app;
  if (!QCoreApplication::instance()) {
    static int argc = 1;
    static char arg0[] = "core_tests";
    static char* argv[] = {arg0, nullptr};
    app = std::make_unique<QCoreApplication>(argc, argv);
  }

  const QImage image = makePattern(333, 211, 99u);
  const std::vector<PdfWriter::Image> pages = {makePdfImage(image, PdfWriter::Image::Encoding::JBIG2, false),
                                               makePdfImage(image, PdfWriter::Image::Encoding::JBIG2, true),
                                               makePdfImage(image, PdfWriter::Image::Encoding::CCITT_G4, false),
                                               makePdfImage(image, PdfWriter::Image::Encoding::CCITT_G4, true)};
  const QByteArray pdf = makePdf(pages);
  BOOST_REQUIRE(!pdf.isEmpty());

  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QFile pdfFile(dir.filePath("test.pdf"));
  BOOST_REQUIRE(pdfFile.open(QIODevice::WriteOnly));
  BOOST_REQUIRE(pdfFile.write(pdf) == pdf.size());
  pdfFile.close();

  // At 1:1, Poppler smooths images (not masks) when drawing them, which shifts edges by a
  // pixel.  From four times the size on it doesn't, so render at 4 x 72 dpi and compare
  // the centre of each 4 x 4 block with the original pixel.
  const int scale = 4;
  QProcess process;
  process.start(pdftoppm, {"-r", QString::number(72 * scale), "-gray", "-aa", "no", "-aaVector", "no",
                           pdfFile.fileName(), dir.filePath("page")});
  BOOST_REQUIRE(process.waitForFinished(60000));
  BOOST_REQUIRE_MESSAGE(process.exitCode() == 0, process.readAllStandardError().toStdString());

  const char* names[] = {"JBIG2 image", "JBIG2 mask", "G4 image", "G4 mask"};
  for (int i = 0; i < 4; ++i) {
    const QImage rendered(dir.filePath(QString("page-%1.pgm").arg(i + 1)));
    BOOST_REQUIRE_MESSAGE(!rendered.isNull(), "page " << i + 1 << " not rendered");
    BOOST_REQUIRE_MESSAGE(rendered.size() == image.size() * scale,
                          names[i] << ": rendered at " << rendered.width() << " x " << rendered.height());
    long mismatches = 0;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        if (isBlack(rendered, x * scale + scale / 2, y * scale + scale / 2) != isBlack(image, x, y)) {
          ++mismatches;
        }
      }
    }
    BOOST_CHECK_MESSAGE(mismatches == 0, names[i] << ": " << mismatches << " pixels differ");
  }
}

BOOST_AUTO_TEST_SUITE_END()
