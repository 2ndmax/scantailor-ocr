// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfImageEncoder.h"

#include <tiffio.h>

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <algorithm>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <vector>

#include "ImageLoadErrors.h"
#include "NonCopyable.h"
#include "TiffReader.h"
#include "jbig2enc/jbig2arith.h"

extern "C" {
#include <jpeglib.h>
}

namespace {
/*============================ libtiff I/O on a QIODevice ============================*/

tsize_t deviceRead(thandle_t context, tdata_t data, tsize_t size) {
  auto* dev = static_cast<QIODevice*>(context);
  return static_cast<tsize_t>(dev->read(static_cast<char*>(data), size));
}

tsize_t deviceWrite(thandle_t context, tdata_t data, tsize_t size) {
  auto* dev = static_cast<QIODevice*>(context);
  return static_cast<tsize_t>(dev->write(static_cast<const char*>(data), size));
}

toff_t deviceSeek(thandle_t context, toff_t offset, int whence) {
  auto* dev = static_cast<QIODevice*>(context);
  qint64 pos = 0;
  switch (whence) {
    case SEEK_SET:
      pos = static_cast<qint64>(offset);
      break;
    case SEEK_CUR:
      pos = dev->pos() + static_cast<qint64>(offset);
      break;
    case SEEK_END:
      pos = dev->size() + static_cast<qint64>(offset);
      break;
    default:
      return static_cast<toff_t>(-1);
  }
  if (!dev->seek(pos)) {
    return static_cast<toff_t>(-1);
  }
  return static_cast<toff_t>(dev->pos());
}

int deviceClose(thandle_t) {
  // The device belongs to the caller.
  return 0;
}

toff_t deviceSize(thandle_t context) {
  return static_cast<toff_t>(static_cast<QIODevice*>(context)->size());
}

int deviceMap(thandle_t, tdata_t*, toff_t*) {
  return 0;
}

void deviceUnmap(thandle_t, tdata_t, toff_t) {}

TIFF* openTiff(QIODevice& device, const char* mode) {
  TiffReader::installMessageHandlers();
  return TIFFClientOpen("PdfImageEncoder", mode, &device, &deviceRead, &deviceWrite, &deviceSeek, &deviceClose,
                        &deviceSize, &deviceMap, &deviceUnmap);
}

class TiffCloser {
 public:
  explicit TiffCloser(TIFF* tif) : m_tif(tif) {}

  ~TiffCloser() {
    if (m_tif) {
      TIFFClose(m_tif);
    }
  }

  TiffCloser(const TiffCloser&) = delete;
  TiffCloser& operator=(const TiffCloser&) = delete;

 private:
  TIFF* m_tif;
};

/*================================= libjpeg helpers =================================*/

class JpegErrorManager : public jpeg_error_mgr {
  DECLARE_NON_COPYABLE(JpegErrorManager)

 public:
  JpegErrorManager() : jpeg_error_mgr() {
    jpeg_std_error(this);
    error_exit = &JpegErrorManager::errorExit;
  }

  jmp_buf& jmpBuf() { return m_jmpBuf; }

 private:
  static void errorExit(j_common_ptr cinfo) {
    char message[JMSG_LENGTH_MAX] = {};
    (*cinfo->err->format_message)(cinfo, message);
    ImageLoadErrorCapture::addError(QString::fromLocal8Bit(message));
    longjmp(static_cast<JpegErrorManager*>(cinfo->err)->m_jmpBuf, 1);
  }

  jmp_buf m_jmpBuf{};
};


class JpegDestination : public jpeg_destination_mgr {
  DECLARE_NON_COPYABLE(JpegDestination)

 public:
  explicit JpegDestination(QByteArray& out) : jpeg_destination_mgr(), m_out(out), m_buffer(64 * 1024) {
    init_destination = &JpegDestination::initDestination;
    empty_output_buffer = &JpegDestination::emptyOutputBuffer;
    term_destination = &JpegDestination::termDestination;
  }

 private:
  static JpegDestination* object(j_compress_ptr cinfo) { return static_cast<JpegDestination*>(cinfo->dest); }

  static void initDestination(j_compress_ptr cinfo) {
    JpegDestination* self = object(cinfo);
    self->next_output_byte = self->m_buffer.data();
    self->free_in_buffer = self->m_buffer.size();
  }

  static boolean emptyOutputBuffer(j_compress_ptr cinfo) {
    // libjpeg ignores free_in_buffer here: the whole buffer is full.
    JpegDestination* self = object(cinfo);
    self->m_out.append(reinterpret_cast<const char*>(self->m_buffer.data()),
                       static_cast<qsizetype>(self->m_buffer.size()));
    self->next_output_byte = self->m_buffer.data();
    self->free_in_buffer = self->m_buffer.size();
    return TRUE;
  }

  static void termDestination(j_compress_ptr cinfo) {
    JpegDestination* self = object(cinfo);
    const size_t used = self->m_buffer.size() - self->free_in_buffer;
    self->m_out.append(reinterpret_cast<const char*>(self->m_buffer.data()), static_cast<qsizetype>(used));
  }

  QByteArray& m_out;
  std::vector<JOCTET> m_buffer;
};

/**
 * Converts the image to 1 bit if necessary.  \a oneIsBlack tells whether bit
 * value 1 is the darker of the two colours, which depends on the colour table.
 */
QImage toMono(const QImage& image, bool* oneIsBlack) {
  QImage mono = image;
  if (mono.format() != QImage::Format_Mono) {
    mono = mono.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither);
  }
  *oneIsBlack = true;
  if (mono.colorCount() >= 2) {
    *oneIsBlack = qGray(mono.color(1)) < qGray(mono.color(0));
  }
  return mono;
}

/*============================ JBIG2 segments ============================*/

void appendU32(QByteArray& out, const uint32_t value) {
  out.append(static_cast<char>((value >> 24) & 0xFF));
  out.append(static_cast<char>((value >> 16) & 0xFF));
  out.append(static_cast<char>((value >> 8) & 0xFF));
  out.append(static_cast<char>(value & 0xFF));
}

/** A segment header (T.88, 7.2) on page 1 without referred-to segments. */
void appendSegmentHeader(QByteArray& out, const uint32_t number, const int type, const uint32_t dataLength) {
  appendU32(out, number);
  out.append(static_cast<char>(type));  // Flags: segment type, 1 byte page association.
  out.append(static_cast<char>(0));     // No referred-to segments.
  out.append(static_cast<char>(1));     // Page association: page 1.
  appendU32(out, dataLength);
}

class Jbig2Context {
  DECLARE_NON_COPYABLE(Jbig2Context)

 public:
  Jbig2Context() : m_ctx(std::make_unique<jbig2enc_ctx>()) { jbig2enc_init(m_ctx.get()); }

  ~Jbig2Context() { jbig2enc_dealloc(m_ctx.get()); }

  jbig2enc_ctx* get() { return m_ctx.get(); }

 private:
  // About 64 KiB, too much for the stack.
  std::unique_ptr<jbig2enc_ctx> m_ctx;
};
}  // namespace

bool PdfImageEncoder::isBitonal(const QImage& image) {
  return image.depth() == 1;
}

QByteArray PdfImageEncoder::encodeG4(const QImage& image) {
  if (image.isNull()) {
    return QByteArray();
  }

  // In the encoded data, 1 means black.
  bool oneIsBlack = true;
  const QImage mono = toMono(image, &oneIsBlack);

  const int width = mono.width();
  const int height = mono.height();
  const int bytesPerRow = (width + 7) / 8;

  QBuffer buffer;
  buffer.open(QIODevice::ReadWrite);
  {
    TIFF* tif = openTiff(buffer, "wm");
    if (!tif) {
      return QByteArray();
    }
    TiffCloser closer(tif);

    TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(width));
    TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(height));
    TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, static_cast<uint16_t>(1));
    TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
    TIFFSetField(tif, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE);
    TIFFSetField(tif, TIFFTAG_FILLORDER, FILLORDER_MSB2LSB);
    TIFFSetField(tif, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4);
    // A single strip, so that its raw data is exactly the G4 stream PDF expects.
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, static_cast<uint32_t>(height));

    std::vector<uint8_t> row(static_cast<size_t>(bytesPerRow));
    for (int y = 0; y < height; ++y) {
      const uchar* line = mono.constScanLine(y);
      for (int i = 0; i < bytesPerRow; ++i) {
        row[i] = oneIsBlack ? line[i] : static_cast<uint8_t>(~line[i]);
      }
      if (TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), 0) < 0) {
        return QByteArray();
      }
    }
    if (!TIFFFlush(tif)) {
      return QByteArray();
    }
  }

  buffer.seek(0);
  TIFF* tif = openTiff(buffer, "rm");
  if (!tif) {
    return QByteArray();
  }
  TiffCloser closer(tif);
  if (TIFFNumberOfStrips(tif) != 1) {
    ImageLoadErrorCapture::addError(QCoreApplication::translate("PdfImageEncoder", "Unexpected G4 strip layout."));
    return QByteArray();
  }
  const uint64_t size = TIFFRawStripSize64(tif, 0);
  if ((size == 0) || (size == static_cast<uint64_t>(-1)) || (size > static_cast<uint64_t>(INT32_MAX))) {
    return QByteArray();
  }
  QByteArray data(static_cast<qsizetype>(size), Qt::Uninitialized);
  if (TIFFReadRawStrip(tif, 0, data.data(), static_cast<tmsize_t>(size)) != static_cast<tmsize_t>(size)) {
    return QByteArray();
  }
  return data;
}  // PdfImageEncoder::encodeG4

QByteArray PdfImageEncoder::encodeJbig2(const QImage& image) {
  if (image.isNull()) {
    return QByteArray();
  }

  // In JBIG2, 1 means black.
  bool oneIsBlack = true;
  const QImage mono = toMono(image, &oneIsBlack);

  const int width = mono.width();
  const int height = mono.height();
  const int bytesPerRow = (width + 7) / 8;

  // jbig2enc expects rows of 32 bit words in native byte order, the leftmost
  // pixel in the most significant bit, and the padding bits set to zero.
  const int wordsPerRow = (width + 31) / 32;
  const uint32_t lastWordMask = (width % 32 == 0) ? 0xFFFFFFFFu : ~(0xFFFFFFFFu >> (width % 32));
  std::vector<uint32_t> words(static_cast<size_t>(wordsPerRow) * static_cast<size_t>(height));
  for (int y = 0; y < height; ++y) {
    const uchar* line = mono.constScanLine(y);
    uint32_t* row = &words[static_cast<size_t>(y) * static_cast<size_t>(wordsPerRow)];
    for (int w = 0; w < wordsPerRow; ++w) {
      uint32_t word = 0;
      for (int b = 0; b < 4; ++b) {
        const int i = w * 4 + b;
        uint8_t byte = 0;
        if (i < bytesPerRow) {
          byte = oneIsBlack ? line[i] : static_cast<uint8_t>(~line[i]);
        }
        word = (word << 8) | byte;
      }
      row[w] = word;
    }
    row[wordsPerRow - 1] &= lastWordMask;
  }

  QByteArray coded;
  {
    Jbig2Context ctx;
    // Typical prediction (TPGDON) skips rows that repeat the previous one, like blank lines.
    jbig2enc_bitimage(ctx.get(), reinterpret_cast<const uint8_t*>(words.data()), width, height, true);
    jbig2enc_final(ctx.get());
    coded.resize(static_cast<qsizetype>(jbig2enc_datasize(ctx.get())));
    jbig2enc_tobuffer(ctx.get(), reinterpret_cast<uint8_t*>(coded.data()));
  }

  QByteArray out;
  // Page information segment (T.88, 7.4.8).
  const int pageInfoLength = 19;
  appendSegmentHeader(out, 0, 48, pageInfoLength);
  appendU32(out, static_cast<uint32_t>(width));
  appendU32(out, static_cast<uint32_t>(height));
  appendU32(out, 0);                    // Horizontal resolution: unknown.
  appendU32(out, 0);                    // Vertical resolution: unknown.
  out.append(static_cast<char>(0x01));  // Flags: lossless, default pixel 0, combination OR.
  out.append(static_cast<char>(0));     // Striping: none.
  out.append(static_cast<char>(0));

  // Immediate generic region segment (T.88, 7.4.6), as written by jbig2enc.
  const int regionHeaderLength = 17 + 1 + 8;
  appendSegmentHeader(out, 1, 38, static_cast<uint32_t>(regionHeaderLength + coded.size()));
  appendU32(out, static_cast<uint32_t>(width));
  appendU32(out, static_cast<uint32_t>(height));
  appendU32(out, 0);                    // x
  appendU32(out, 0);                    // y
  out.append(static_cast<char>(0));     // External combination operator: OR.
  out.append(static_cast<char>(0x08));  // MMR off, template 0, TPGDON on.
  // The adaptive template pixels in their nominal positions.
  const signed char at[] = {3, -1, -3, -1, 2, -2, -2, -2};
  out.append(reinterpret_cast<const char*>(at), sizeof(at));
  out.append(coded);
  return out;
}  // PdfImageEncoder::encodeJbig2

QByteArray PdfImageEncoder::encodeJpeg(const QImage& image, const int quality, int* components) {
  if (image.isNull()) {
    return QByteArray();
  }

  const bool gray = image.isGrayscale();
  const QImage src = image.convertToFormat(gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
  const int numComponents = gray ? 1 : 3;

  // Everything with a destructor has to exist before setjmp().
  QByteArray out;
  JpegErrorManager errorManager;
  JpegDestination destination(out);
  jpeg_compress_struct cinfo{};
  cinfo.err = &errorManager;

  if (setjmp(errorManager.jmpBuf())) {
    jpeg_destroy_compress(&cinfo);
    return QByteArray();
  }

  jpeg_create_compress(&cinfo);
  cinfo.dest = &destination;
  cinfo.image_width = static_cast<JDIMENSION>(src.width());
  cinfo.image_height = static_cast<JDIMENSION>(src.height());
  cinfo.input_components = numComponents;
  cinfo.in_color_space = gray ? JCS_GRAYSCALE : JCS_RGB;
  jpeg_set_defaults(&cinfo);
  jpeg_set_quality(&cinfo, qBound(1, quality, 100), TRUE);
  cinfo.optimize_coding = TRUE;

  jpeg_start_compress(&cinfo, TRUE);
  while (cinfo.next_scanline < cinfo.image_height) {
    auto* row = const_cast<JSAMPROW>(src.constScanLine(static_cast<int>(cinfo.next_scanline)));
    jpeg_write_scanlines(&cinfo, &row, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);

  if (components) {
    *components = numComponents;
  }
  return out;
}  // PdfImageEncoder::encodeJpeg

QRect PdfImageEncoder::contentRect(const QImage& image) {
  if (image.isNull()) {
    return QRect();
  }
  // Pixels at least this bright in every channel count as paper.
  const int threshold = 250;

  const bool gray = image.isGrayscale();
  const QImage img = image.convertToFormat(gray ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
  const int width = img.width();
  const int height = img.height();

  auto isContent = [&](const uchar* line, const int x) -> bool {
    if (gray) {
      return line[x] < threshold;
    }
    const QRgb rgb = reinterpret_cast<const QRgb*>(line)[x];
    return (qRed(rgb) < threshold) || (qGreen(rgb) < threshold) || (qBlue(rgb) < threshold);
  };

  int left = width;
  int right = -1;
  int top = height;
  int bottom = -1;
  for (int y = 0; y < height; ++y) {
    const uchar* line = img.constScanLine(y);
    int x = 0;
    while ((x < width) && !isContent(line, x)) {
      ++x;
    }
    if (x == width) {
      continue;
    }
    left = std::min(left, x);
    int xr = width - 1;
    while (!isContent(line, xr)) {
      --xr;
    }
    right = std::max(right, xr);
    top = std::min(top, y);
    bottom = y;
  }

  if (right < 0) {
    return QRect();
  }
  return QRect(QPoint(left, top), QPoint(right, bottom));
}  // PdfImageEncoder::contentRect

bool PdfImageEncoder::readTiffInfo(const QString& filePath, QSize* size, int* bitsPerPixel) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  TIFF* tif = openTiff(file, "rm");
  if (!tif) {
    return false;
  }
  TiffCloser closer(tif);

  uint32_t width = 0;
  uint32_t height = 0;
  uint16_t bitsPerSample = 1;
  uint16_t samplesPerPixel = 1;
  if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &width) || !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &height)) {
    return false;
  }
  TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);

  if (size) {
    *size = QSize(static_cast<int>(width), static_cast<int>(height));
  }
  if (bitsPerPixel) {
    *bitsPerPixel = bitsPerSample * samplesPerPixel;
  }
  return true;
}
