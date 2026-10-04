// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TiffReader.h"

#include <tiff.h>
#include <tiffio.h>

#include <QCoreApplication>
#include <QDebug>
#include <QIODevice>
#include <QImage>
#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <new>
#include <vector>

#include "Dpm.h"
#include "ImageLoadErrors.h"
#include "ImageMetadata.h"
#include "NonCopyable.h"

class TiffReader::TiffHeader {
 public:
  enum Signature { INVALID_SIGNATURE, TIFF_BIG_ENDIAN, TIFF_LITTLE_ENDIAN };

  TiffHeader() : m_signature(INVALID_SIGNATURE), m_version(0) {}

  TiffHeader(Signature signature, int version) : m_signature(signature), m_version(version) {}

  Signature signature() const { return m_signature; }

  int version() const { return m_version; }

 private:
  Signature m_signature;
  int m_version;
};


class TiffReader::TiffHandle {
 public:
  explicit TiffHandle(TIFF* handle) : m_handle(handle) {}

  ~TiffHandle() {
    if (m_handle) {
      TIFFClose(m_handle);
    }
  }

  TIFF* handle() const { return m_handle; }

 private:
  TIFF* m_handle;
};


template <typename T>
class TiffReader::TiffBuffer {
  DECLARE_NON_COPYABLE(TiffBuffer)

 public:
  TiffBuffer() : m_data(nullptr) {}

  explicit TiffBuffer(size_t numItems) {
    // The multiplication is done in size_t to avoid an overflow, which would
    // silently allocate a buffer that's too small for the data we're about
    // to put into it.
    const size_t numBytes = numItems * sizeof(T);
    if ((numItems != 0) && ((numBytes / numItems) != sizeof(T))) {
      throw std::bad_alloc();
    }
    // libtiff sizes allocations with a signed type, which may be narrower
    // than size_t.  Make sure the value survives the conversion.
    const auto requestedSize = static_cast<tsize_t>(numBytes);
    if ((requestedSize < 0) || (static_cast<size_t>(requestedSize) != numBytes)) {
      throw std::bad_alloc();
    }
    m_data = (T*) _TIFFmalloc(requestedSize);
    if (!m_data) {
      throw std::bad_alloc();
    }
  }

  ~TiffBuffer() {
    if (m_data) {
      _TIFFfree(m_data);
    }
  }

  T* data() { return m_data; }

  void swap(TiffBuffer& other) { std::swap(m_data, other.m_data); }

 private:
  T* m_data;
};


struct TiffReader::TiffInfo {
  int width;
  int height;
  uint16_t bitsPerSample;
  uint16_t samplesPerPixel;
  uint16_t sampleFormat;
  uint16_t photometric;
  uint16_t compression;
  uint16_t planarConfig;

  explicit TiffInfo(const TiffHandle& tif);

  bool mapsToBinaryOrIndexed8() const;

  bool needsNumericConversion() const;
};


TiffReader::TiffInfo::TiffInfo(const TiffHandle& tif)
    : width(0),
      height(0),
      bitsPerSample(1),
      samplesPerPixel(1),
      sampleFormat(SAMPLEFORMAT_UINT),
      photometric(PHOTOMETRIC_MINISBLACK),
      compression(COMPRESSION_NONE),
      planarConfig(PLANARCONFIG_CONTIG) {
  TIFFGetField(tif.handle(), TIFFTAG_COMPRESSION, &compression);
  switch (compression) {
    case COMPRESSION_CCITTFAX3:
    case COMPRESSION_CCITTFAX4:
    case COMPRESSION_CCITTRLE:
    case COMPRESSION_CCITTRLEW:
      photometric = PHOTOMETRIC_MINISWHITE;
      break;
    default:
      break;
  }

  uint32_t w = 0;
  uint32_t h = 0;
  TIFFGetField(tif.handle(), TIFFTAG_IMAGEWIDTH, &w);
  TIFFGetField(tif.handle(), TIFFTAG_IMAGELENGTH, &h);
  if ((w <= static_cast<uint32_t>(INT_MAX)) && (h <= static_cast<uint32_t>(INT_MAX))) {
    width = static_cast<int>(w);
    height = static_cast<int>(h);
  }

  TIFFGetField(tif.handle(), TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
  TIFFGetField(tif.handle(), TIFFTAG_SAMPLESPERPIXEL, &samplesPerPixel);
  TIFFGetField(tif.handle(), TIFFTAG_SAMPLEFORMAT, &sampleFormat);
  TIFFGetField(tif.handle(), TIFFTAG_PHOTOMETRIC, &photometric);
  TIFFGetFieldDefaulted(tif.handle(), TIFFTAG_PLANARCONFIG, &planarConfig);

  if (sampleFormat == SAMPLEFORMAT_VOID) {
    // "Untyped data" is treated as unsigned integers by libtiff as well.
    sampleFormat = SAMPLEFORMAT_UINT;
  }
}

bool TiffReader::TiffInfo::mapsToBinaryOrIndexed8() const {
  // Note: bitsPerSample == 0 has to be rejected as well.  Not only is it
  // meaningless, it would also lead to a division by zero when building the
  // color table in extractBinaryOrIndexed8Image().
  if ((samplesPerPixel != 1) || (sampleFormat != SAMPLEFORMAT_UINT) || (bitsPerSample < 1) || (bitsPerSample > 8)) {
    return false;
  }

  switch (photometric) {
    case PHOTOMETRIC_PALETTE:
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MINISWHITE:
      return true;
    default:
      break;
  }
  return false;
}

bool TiffReader::TiffInfo::needsNumericConversion() const {
  switch (photometric) {
    case PHOTOMETRIC_MINISBLACK:
    case PHOTOMETRIC_MINISWHITE:
      if (samplesPerPixel < 1) {
        return false;
      }
      break;
    case PHOTOMETRIC_RGB:
      if (samplesPerPixel < 3) {
        return false;
      }
      break;
    default:
      return false;
  }

  switch (sampleFormat) {
    case SAMPLEFORMAT_IEEEFP:
      return (bitsPerSample == 16) || (bitsPerSample == 32) || (bitsPerSample == 64);
    case SAMPLEFORMAT_INT:
      return (bitsPerSample == 8) || (bitsPerSample == 16) || (bitsPerSample == 32);
    case SAMPLEFORMAT_UINT:
      // Up to 16 bits are handled by the RGBA interface.
      return bitsPerSample == 32;
    default:
      break;
  }
  return false;
}

namespace {
QString formatTiffMessage(const char* module, const char* fmt, va_list ap) {
  char buf[1024];
  std::vsnprintf(buf, sizeof(buf), fmt, ap);
  QString message = QString::fromLocal8Bit(buf);
  if (module && *module) {
    message = QString::fromLocal8Bit(module) + QLatin1String(": ") + message;
  }
  return message;
}

void tiffErrorHandler(const char* module, const char* fmt, va_list ap) {
  ImageLoadErrorCapture::addError(formatTiffMessage(module, fmt, ap));
}

void tiffWarningHandler(const char* module, const char* fmt, va_list ap) {
  ImageLoadErrorCapture::addWarning(formatTiffMessage(module, fmt, ap));
}

/**
 * libtiff's default handlers print to stderr, which is invisible in a GUI
 * application.  Ours forward the messages to ImageLoadErrorCapture, so they
 * can be shown to the user.
 */
void ensureTiffHandlersInstalled() {
  static const bool installed = []() {
    TIFFSetErrorHandler(&tiffErrorHandler);
    TIFFSetWarningHandler(&tiffWarningHandler);
    return true;
  }();
  (void) installed;
}

QString unsupportedCompressionMessage(const uint16_t compression) {
  return QCoreApplication::translate("TiffReader",
                                     "The compression method \"%1\" is not supported by the libtiff library "
                                     "this program was built with.")
      .arg(TiffReader::compressionName(compression));
}

/**
 * \brief Decodes the image row by row, regardless of whether it's stored
 *        in strips or in tiles.
 *
 * Unlike TIFFReadScanline(), this works with tiled images and doesn't
 * degrade to quadratic complexity with separate sample planes, because
 * whole strips / tiles are decoded at once.
 *
 * \param numPlanes The number of sample planes to deliver.  Must be 1 for
 *        PLANARCONFIG_CONTIG, and at most SamplesPerPixel otherwise.
 * \param bitsPerPixel The number of bits per pixel within one plane.
 * \param rowFunc Called as rowFunc(y, planeRows) for each row, top to bottom.
 *        planeRows[p] points to the packed samples of plane p.
 */
template <typename RowFunc>
bool readRasterRows(TIFF* tif,
                    const int width,
                    const int height,
                    const int numPlanes,
                    const int bitsPerPixel,
                    RowFunc rowFunc) {
  const uint64_t rowBytes64 = TIFFScanlineSize64(tif);
  if (rowBytes64 == 0) {
    return false;
  }
  // Make sure a row really holds enough bits for a full row of pixels,
  // or the callers would read past the end of the buffer.
  if (rowBytes64 * 8 < static_cast<uint64_t>(width) * static_cast<uint64_t>(bitsPerPixel)) {
    ImageLoadErrorCapture::addError(QCoreApplication::translate("TiffReader", "Inconsistent image layout."));
    return false;
  }
  const auto rowBytes = static_cast<size_t>(rowBytes64);

  std::vector<std::vector<uint8_t>> planes(numPlanes);
  std::vector<const uint8_t*> planeRows(numPlanes);

  auto bandBytes = [&](const uint64_t rows) -> size_t {
    const uint64_t bytes = rows * rowBytes64;
    if ((rowBytes64 != 0) && ((bytes / rowBytes64 != rows) || (bytes > std::numeric_limits<size_t>::max() / 2))) {
      throw std::bad_alloc();
    }
    return static_cast<size_t>(bytes);
  };

  if (TIFFIsTiled(tif)) {
    uint32_t tileWidth = 0;
    uint32_t tileHeight = 0;
    TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tileWidth);
    TIFFGetField(tif, TIFFTAG_TILELENGTH, &tileHeight);
    const tmsize_t tileSize = TIFFTileSize(tif);
    const uint64_t tileRowBytes = TIFFTileRowSize64(tif);
    if ((tileWidth == 0) || (tileHeight == 0) || (tileSize <= 0) || (tileRowBytes == 0)
        || (static_cast<uint64_t>(tileSize) < tileRowBytes * std::min<uint64_t>(tileHeight, height))) {
      ImageLoadErrorCapture::addError(QCoreApplication::translate("TiffReader", "Invalid tile layout."));
      return false;
    }
    const uint32_t bandRows = std::min<uint32_t>(tileHeight, static_cast<uint32_t>(height));

    std::vector<uint8_t> tile(static_cast<size_t>(tileSize));
    for (auto& plane : planes) {
      plane.resize(bandBytes(bandRows));
    }

    for (uint32_t y0 = 0; y0 < static_cast<uint32_t>(height); y0 += tileHeight) {
      const uint32_t rows = std::min<uint32_t>(tileHeight, static_cast<uint32_t>(height) - y0);
      for (int p = 0; p < numPlanes; ++p) {
        for (uint32_t x0 = 0; x0 < static_cast<uint32_t>(width); x0 += tileWidth) {
          if (TIFFReadTile(tif, tile.data(), x0, y0, 0, static_cast<uint16_t>(p)) < 0) {
            return false;
          }
          // Tile widths are multiples of 16, so a tile always starts at a byte boundary.
          const uint64_t offset = static_cast<uint64_t>(x0) * bitsPerPixel / 8;
          if (offset >= rowBytes64) {
            break;
          }
          const auto numBytes = static_cast<size_t>(std::min<uint64_t>(tileRowBytes, rowBytes64 - offset));
          for (uint32_t r = 0; r < rows; ++r) {
            std::memcpy(planes[p].data() + r * rowBytes + offset, tile.data() + r * tileRowBytes, numBytes);
          }
        }
      }
      for (uint32_t r = 0; r < rows; ++r) {
        for (int p = 0; p < numPlanes; ++p) {
          planeRows[p] = planes[p].data() + r * rowBytes;
        }
        rowFunc(static_cast<int>(y0 + r), planeRows.data());
      }
    }
  } else {
    uint32_t rowsPerStrip = 0;
    TIFFGetFieldDefaulted(tif, TIFFTAG_ROWSPERSTRIP, &rowsPerStrip);
    if ((rowsPerStrip == 0) || (rowsPerStrip > static_cast<uint32_t>(height))) {
      rowsPerStrip = static_cast<uint32_t>(height);
    }

    for (auto& plane : planes) {
      plane.resize(bandBytes(rowsPerStrip));
    }

    for (uint32_t y0 = 0; y0 < static_cast<uint32_t>(height); y0 += rowsPerStrip) {
      const uint32_t rows = std::min<uint32_t>(rowsPerStrip, static_cast<uint32_t>(height) - y0);
      const tmsize_t expected = static_cast<tmsize_t>(bandBytes(rows));
      for (int p = 0; p < numPlanes; ++p) {
        const uint32_t strip = TIFFComputeStrip(tif, y0, static_cast<uint16_t>(p));
        const tmsize_t numRead = TIFFReadEncodedStrip(tif, strip, planes[p].data(), expected);
        if (numRead < 0) {
          return false;
        }
        if (numRead < expected) {
          // A short strip.  Don't expose whatever was left in the buffer.
          std::memset(planes[p].data() + numRead, 0, static_cast<size_t>(expected - numRead));
        }
      }
      for (uint32_t r = 0; r < rows; ++r) {
        for (int p = 0; p < numPlanes; ++p) {
          planeRows[p] = planes[p].data() + r * rowBytes;
        }
        rowFunc(static_cast<int>(y0 + r), planeRows.data());
      }
    }
  }
  return true;
}  // readRasterRows

using SampleReader = double (*)(const uint8_t* row, size_t index);

template <typename T>
double readTypedSample(const uint8_t* row, const size_t index) {
  T value;
  std::memcpy(&value, row + index * sizeof(T), sizeof(T));
  return static_cast<double>(value);
}

double halfToDouble(const uint16_t half) {
  const bool negative = (half & 0x8000) != 0;
  const int exponent = (half >> 10) & 0x1f;
  const int mantissa = half & 0x3ff;
  double value;
  if (exponent == 0) {
    value = std::ldexp(static_cast<double>(mantissa), -24);
  } else if (exponent == 0x1f) {
    value = (mantissa != 0) ? std::numeric_limits<double>::quiet_NaN() : std::numeric_limits<double>::infinity();
  } else {
    value = std::ldexp(static_cast<double>(mantissa | 0x400), exponent - 25);
  }
  return negative ? -value : value;
}

double readHalfSample(const uint8_t* row, const size_t index) {
  uint16_t half;
  std::memcpy(&half, row + index * sizeof(half), sizeof(half));
  return halfToDouble(half);
}

SampleReader sampleReaderFor(const uint16_t sampleFormat, const uint16_t bitsPerSample) {
  switch (sampleFormat) {
    case SAMPLEFORMAT_IEEEFP:
      switch (bitsPerSample) {
        case 16:
          return &readHalfSample;
        case 32:
          return &readTypedSample<float>;
        case 64:
          return &readTypedSample<double>;
        default:
          break;
      }
      break;
    case SAMPLEFORMAT_INT:
      switch (bitsPerSample) {
        case 8:
          return &readTypedSample<int8_t>;
        case 16:
          return &readTypedSample<int16_t>;
        case 32:
          return &readTypedSample<int32_t>;
        default:
          break;
      }
      break;
    case SAMPLEFORMAT_UINT:
      switch (bitsPerSample) {
        case 8:
          return &readTypedSample<uint8_t>;
        case 16:
          return &readTypedSample<uint16_t>;
        case 32:
          return &readTypedSample<uint32_t>;
        default:
          break;
      }
      break;
    default:
      break;
  }
  return nullptr;
}
}  // namespace

static tsize_t deviceRead(thandle_t context, tdata_t data, tsize_t size) {
  auto* dev = (QIODevice*) context;
  return (tsize_t) dev->read(static_cast<char*>(data), size);
}

static tsize_t deviceWrite(thandle_t context, tdata_t data, tsize_t size) {
  // Not implemented.
  return 0;
}

static toff_t deviceSeek(thandle_t context, toff_t offset, int whence) {
  auto* dev = (QIODevice*) context;

  switch (whence) {
    case SEEK_SET:
      dev->seek(offset);
      break;
    case SEEK_CUR:
      dev->seek(dev->pos() + offset);
      break;
    case SEEK_END:
      dev->seek(dev->size() + offset);
      break;
    default:
      break;
  }
  return dev->pos();
}

static int deviceClose(thandle_t) {
  // Deliberately a no-op.  libtiff calls this from TIFFClose(), but the
  // device is owned by our caller, who merely lends it to us - closing it
  // here would be a surprising side effect of reading an image.
  return 0;
}

static toff_t deviceSize(thandle_t context) {
  auto* dev = (QIODevice*) context;
  return dev->size();
}

static int deviceMap(thandle_t, tdata_t*, toff_t*) {
  // Not implemented.
  return 0;
}

static void deviceUnmap(thandle_t, tdata_t, toff_t) {
  // Not implemented.
}

bool TiffReader::canRead(QIODevice& device) {
  if (!device.isReadable()) {
    return false;
  }
  if (device.isSequential()) {
    // libtiff needs to be able to seek.
    return false;
  }

  TiffHeader header(readHeader(device));
  return checkHeader(header);
}

ImageMetadataLoader::Status TiffReader::readMetadata(QIODevice& device,
                                                     const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!device.isReadable()) {
    return ImageMetadataLoader::GENERIC_ERROR;
  }
  if (device.isSequential()) {
    // libtiff needs to be able to seek.
    return ImageMetadataLoader::GENERIC_ERROR;
  }

  if (!checkHeader(TiffHeader(readHeader(device)))) {
    return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;
  }

  ensureTiffHandlersInstalled();

  TiffHandle tif(TIFFClientOpen("TIFF", "rBm", &device, &deviceRead, &deviceWrite, &deviceSeek, &deviceClose,
                                &deviceSize, &deviceMap, &deviceUnmap));
  if (!tif.handle()) {
    return ImageMetadataLoader::GENERIC_ERROR;
  }

  // Pages are only reported once all of them turned out to be readable.
  // A page we would fail to decode later is better rejected right away,
  // while the user is still importing files.
  std::vector<ImageMetadata> pages;
  do {
    uint16_t compression = COMPRESSION_NONE;
    TIFFGetField(tif.handle(), TIFFTAG_COMPRESSION, &compression);
    if (!TIFFIsCODECConfigured(compression)) {
      ImageLoadErrorCapture::addError(unsupportedCompressionMessage(compression));
      return ImageMetadataLoader::GENERIC_ERROR;
    }
    pages.push_back(currentPageMetadata(tif));
  } while (TIFFReadDirectory(tif.handle()));

  for (const ImageMetadata& page : pages) {
    out(page);
  }
  return ImageMetadataLoader::LOADED;
}

static void convertAbgrToArgb(const uint32_t* src, uint32_t* dst, int count) {
  for (int i = 0; i < count; ++i) {
    const uint32_t srcWord = src[i];
    uint32_t dstWord = srcWord & 0xFF000000;  // A
    dstWord |= (srcWord & 0x00FF0000) >> 16;  // B
    dstWord |= srcWord & 0x0000FF00;          // G
    dstWord |= (srcWord & 0x000000FF) << 16;  // R
    dst[i] = dstWord;
  }
}

QImage TiffReader::readImage(QIODevice& device, const int pageNum) {
  if (!device.isReadable()) {
    return QImage();
  }
  if (device.isSequential()) {
    // libtiff needs to be able to seek.
    return QImage();
  }

  TiffHeader header(readHeader(device));
  if (!checkHeader(header)) {
    return QImage();
  }

  ensureTiffHandlersInstalled();

  TiffHandle tif(TIFFClientOpen("TIFF", "rBm", &device, &deviceRead, &deviceWrite, &deviceSeek, &deviceClose,
                                &deviceSize, &deviceMap, &deviceUnmap));
  if (!tif.handle()) {
    return QImage();
  }

  // libtiff addresses directories with a 16-bit type, so anything outside
  // that range has to be rejected rather than silently truncated.
  if ((pageNum < 0) || (pageNum > 0xffff)) {
    return QImage();
  }
  if (!TIFFSetDirectory(tif.handle(), static_cast<uint16_t>(pageNum))) {
    ImageLoadErrorCapture::addError(
        QCoreApplication::translate("TiffReader", "Page %1 doesn't exist in this file.").arg(pageNum + 1));
    return QImage();
  }

  const TiffInfo info(tif);
  if ((info.width <= 0) || (info.height <= 0)) {
    // Missing or nonsensical ImageWidth / ImageLength tags.
    ImageLoadErrorCapture::addError(
        QCoreApplication::translate("TiffReader", "The image dimensions are missing or invalid."));
    return QImage();
  }

  if (!TIFFIsCODECConfigured(info.compression)) {
    ImageLoadErrorCapture::addError(unsupportedCompressionMessage(info.compression));
    return QImage();
  }

  const ImageMetadata metadata(currentPageMetadata(tif));

  QImage image;

  if (info.mapsToBinaryOrIndexed8()) {
    // Common case optimization.
    image = extractBinaryOrIndexed8Image(tif, info);
    if (image.isNull()) {
      // Last resort: libtiff's RGBA interface copes with a few more exotic
      // layouts and is more lenient with damaged data.
      image = readRgbaImage(tif, info);
    }
  } else if (info.needsNumericConversion()) {
    image = extractNumericImage(tif, info);
  } else {
    // General case.
    image = readRgbaImage(tif, info);
  }

  if (image.isNull()) {
    return QImage();
  }

  if (!metadata.dpi().isNull()) {
    const Dpm dpm(metadata.dpi());
    image.setDotsPerMeterX(dpm.horizontal());
    image.setDotsPerMeterY(dpm.vertical());
  }
  return image;
}  // TiffReader::readImage

void TiffReader::installMessageHandlers() {
  ensureTiffHandlersInstalled();
}

QString TiffReader::compressionName(const unsigned compression) {
  if (compression <= 0xffff) {
    if (const TIFFCodec* codec = TIFFFindCODEC(static_cast<uint16_t>(compression))) {
      if (codec->name && *codec->name) {
        return QString::fromLatin1(codec->name);
      }
    }
  }

  // Schemes libtiff doesn't know about at all.
  switch (compression) {
    case 33003:
    case 33005:
    case 34712:
      return QStringLiteral("JPEG 2000");
    case 34892:
      return QStringLiteral("Lossy JPEG (DNG)");
    case 50002:
    case 52546:
      return QStringLiteral("JPEG XL");
    default:
      break;
  }
  return QStringLiteral("#%1").arg(compression);
}

TiffReader::TiffHeader TiffReader::readHeader(QIODevice& device) {
  unsigned char data[4];
  if (device.peek((char*) data, sizeof(data)) != sizeof(data)) {
    return TiffHeader();
  }

  const uint16_t versionByte0 = data[2];
  const uint16_t versionByte1 = data[3];

  if ((data[0] == 0x4d) && (data[1] == 0x4d)) {
    const uint16_t version = (versionByte0 << 8) + versionByte1;
    return TiffHeader(TiffHeader::TIFF_BIG_ENDIAN, version);
  } else if ((data[0] == 0x49) && (data[1] == 0x49)) {
    const uint16_t version = (versionByte1 << 8) + versionByte0;
    return TiffHeader(TiffHeader::TIFF_LITTLE_ENDIAN, version);
  } else {
    return TiffHeader();
  }
}

bool TiffReader::checkHeader(const TiffHeader& header) {
  if (header.signature() == TiffHeader::INVALID_SIGNATURE) {
    return false;
  }
  if ((header.version() != 42) && (header.version() != 43)) {
    return false;
  }
  return true;
}

ImageMetadata TiffReader::currentPageMetadata(const TiffHandle& tif) {
  uint32_t width = 0, height = 0;
  float xres = 0, yres = 0;
  uint16_t resUnit = 0;
  TIFFGetField(tif.handle(), TIFFTAG_IMAGEWIDTH, &width);
  TIFFGetField(tif.handle(), TIFFTAG_IMAGELENGTH, &height);
  TIFFGetField(tif.handle(), TIFFTAG_XRESOLUTION, &xres);
  TIFFGetField(tif.handle(), TIFFTAG_YRESOLUTION, &yres);
  TIFFGetFieldDefaulted(tif.handle(), TIFFTAG_RESOLUTIONUNIT, &resUnit);
  return ImageMetadata(QSize(width, height), getDpi(xres, yres, resUnit));
}

Dpi TiffReader::getDpi(float xres, float yres, unsigned resUnit) {
  switch (resUnit) {
    case RESUNIT_INCH:  // inch
      return Dpi(qRound(xres), qRound(yres));
    case RESUNIT_CENTIMETER:  // cm
      return Dpm(qRound(xres * 100), qRound(yres * 100));
    default:
      break;
  }
  return Dpi();
}

QImage TiffReader::extractBinaryOrIndexed8Image(const TiffHandle& tif, const TiffInfo& info) {
  QImage::Format format = QImage::Format_Indexed8;
  if (info.bitsPerSample == 1) {
    // Because we specify B option when opening, we can
    // always use Format_Mono, and not Format_MonoLSB.
    format = QImage::Format_Mono;
  }

  QImage image(info.width, info.height, format);
  if (image.isNull()) {
    throw std::bad_alloc();
  }

  const int numColors = 1 << info.bitsPerSample;
  image.setColorCount(numColors);

  if (info.photometric == PHOTOMETRIC_PALETTE) {
    uint16_t* pr = nullptr;
    uint16_t* pg = nullptr;
    uint16_t* pb = nullptr;
    TIFFGetField(tif.handle(), TIFFTAG_COLORMAP, &pr, &pg, &pb);
    if (!pr || !pg || !pb) {
      ImageLoadErrorCapture::addError(
          QCoreApplication::translate("TiffReader", "The color palette of the image is missing."));
      return QImage();
    }
    // Note: libtiff delivers the colormap already converted to the host
    // byte order, so no byte swapping must be done here.

    // Some writers store 8-bit instead of 16-bit palette entries.  Detect
    // that the same way libtiff's RGBA interface does.
    bool eightBitPalette = true;
    for (int i = 0; i < numColors; ++i) {
      if ((pr[i] >= 256) || (pg[i] >= 256) || (pb[i] >= 256)) {
        eightBitPalette = false;
        break;
      }
    }

    const double f = eightBitPalette ? 1.0 : 255.0 / 65535.0;
    for (int i = 0; i < numColors; ++i) {
      const auto r = (uint32_t) std::lround(pr[i] * f);
      const auto g = (uint32_t) std::lround(pg[i] * f);
      const auto b = (uint32_t) std::lround(pb[i] * f);
      const uint32_t a = 0xFF000000;
      image.setColor(i, a | (r << 16) | (g << 8) | b);
    }
  } else if (info.photometric == PHOTOMETRIC_MINISBLACK) {
    const double f = 255.0 / (numColors - 1);
    for (int i = 0; i < numColors; ++i) {
      const auto gray = (int) std::lround(i * f);
      image.setColor(i, qRgb(gray, gray, gray));
    }
  } else if (info.photometric == PHOTOMETRIC_MINISWHITE) {
    const double f = 255.0 / (numColors - 1);
    int c = numColors - 1;
    for (int i = 0; i < numColors; ++i, --c) {
      const auto gray = (int) std::lround(c * f);
      image.setColor(i, qRgb(gray, gray, gray));
    }
  } else {
    return QImage();
  }

  const int width = info.width;
  const int bitsPerSample = info.bitsPerSample;
  const unsigned dstMask = (1u << bitsPerSample) - 1;

  const bool ok = readRasterRows(tif.handle(), width, info.height, 1, bitsPerSample,
                                 [&](const int y, const uint8_t* const* planeRows) {
                                   const uint8_t* src = planeRows[0];
                                   uint8_t* dst = image.scanLine(y);

                                   if (bitsPerSample == 1) {
                                     std::memcpy(dst, src, static_cast<size_t>((width + 7) / 8));
                                   } else if (bitsPerSample == 8) {
                                     std::memcpy(dst, src, static_cast<size_t>(width));
                                   } else {
                                     unsigned accum = 0;
                                     int bitsInAccum = 0;
                                     for (int i = width; i > 0; --i, ++dst) {
                                       while (bitsInAccum < bitsPerSample) {
                                         accum <<= 8;
                                         accum |= *src;
                                         bitsInAccum += 8;
                                         ++src;
                                       }
                                       bitsInAccum -= bitsPerSample;
                                       *dst = static_cast<uint8_t>((accum >> bitsInAccum) & dstMask);
                                     }
                                   }
                                 });

  if (!ok) {
    // QImage doesn't zero its buffer, so a partially read image would
    // expose uninitialized memory.  Report the failure instead.
    return QImage();
  }
  return image;
}  // TiffReader::extractBinaryOrIndexed8Image

QImage TiffReader::extractNumericImage(const TiffHandle& tif, const TiffInfo& info) {
  const SampleReader readSample = sampleReaderFor(info.sampleFormat, info.bitsPerSample);
  if (!readSample) {
    ImageLoadErrorCapture::addError(
        QCoreApplication::translate("TiffReader", "Unsupported sample format (%1 bits per sample, format %2).")
            .arg(info.bitsPerSample)
            .arg(info.sampleFormat));
    return QImage();
  }

  const bool rgb = info.photometric == PHOTOMETRIC_RGB;
  const int channels = rgb ? 3 : 1;
  const bool separate = (info.planarConfig == PLANARCONFIG_SEPARATE) && (info.samplesPerPixel > 1);
  const int numPlanes = separate ? channels : 1;
  const int pixelStride = separate ? 1 : info.samplesPerPixel;
  const int bitsPerPixel = info.bitsPerSample * pixelStride;
  const int width = info.width;
  const int height = info.height;

  auto sampleAt = [&](const uint8_t* const* planeRows, const int x, const int c) -> double {
    if (separate) {
      return readSample(planeRows[c], static_cast<size_t>(x));
    }
    return readSample(planeRows[0], static_cast<size_t>(x) * pixelStride + c);
  };

  // Figure out the range of values to map onto 0..255.
  double lo = 0.0;
  double hi = 0.0;
  bool haveRange = false;

  double sMin = 0.0;
  double sMax = 0.0;
  if (TIFFGetField(tif.handle(), TIFFTAG_SMINSAMPLEVALUE, &sMin)
      && TIFFGetField(tif.handle(), TIFFTAG_SMAXSAMPLEVALUE, &sMax) && std::isfinite(sMin) && std::isfinite(sMax)
      && (sMax > sMin)) {
    lo = sMin;
    hi = sMax;
    haveRange = true;
  }

  if (!haveRange) {
    // No usable hints in the file, so we have to look at the data.
    double minValue = std::numeric_limits<double>::infinity();
    double maxValue = -std::numeric_limits<double>::infinity();
    const bool ok
        = readRasterRows(tif.handle(), width, height, numPlanes, bitsPerPixel, [&](int, const uint8_t* const* rows) {
            for (int x = 0; x < width; ++x) {
              for (int c = 0; c < channels; ++c) {
                const double v = sampleAt(rows, x, c);
                if (std::isfinite(v)) {
                  minValue = std::min(minValue, v);
                  maxValue = std::max(maxValue, v);
                }
              }
            }
          });
    if (!ok) {
      return QImage();
    }

    if (!(minValue <= maxValue)) {
      // Not a single finite value.
      minValue = 0.0;
      maxValue = 1.0;
    }
    if ((info.sampleFormat == SAMPLEFORMAT_IEEEFP) && (minValue >= 0.0) && (maxValue <= 1.0)) {
      // The usual convention for floating point images.
      lo = 0.0;
      hi = 1.0;
    } else {
      lo = minValue;
      hi = maxValue;
    }
  }
  if (!(hi > lo)) {
    hi = lo + 1.0;
  }

  const double scale = 255.0 / (hi - lo);
  const bool invert = info.photometric == PHOTOMETRIC_MINISWHITE;
  auto to8Bit = [&](double v) -> int {
    if (std::isnan(v)) {
      v = lo;
    }
    v = std::min(std::max(v, lo), hi);
    const auto level = static_cast<int>(std::lround((v - lo) * scale));
    return invert ? 255 - level : level;
  };

  QImage image(width, height, rgb ? QImage::Format_RGB32 : QImage::Format_Indexed8);
  if (image.isNull()) {
    throw std::bad_alloc();
  }
  if (!rgb) {
    image.setColorCount(256);
    for (int i = 0; i < 256; ++i) {
      image.setColor(i, qRgb(i, i, i));
    }
  }

  const bool ok = readRasterRows(
      tif.handle(), width, height, numPlanes, bitsPerPixel, [&](const int y, const uint8_t* const* rows) {
        if (rgb) {
          auto* dst = reinterpret_cast<QRgb*>(image.scanLine(y));
          for (int x = 0; x < width; ++x) {
            dst[x] = qRgb(to8Bit(sampleAt(rows, x, 0)), to8Bit(sampleAt(rows, x, 1)), to8Bit(sampleAt(rows, x, 2)));
          }
        } else {
          uint8_t* dst = image.scanLine(y);
          for (int x = 0; x < width; ++x) {
            dst[x] = static_cast<uint8_t>(to8Bit(sampleAt(rows, x, 0)));
          }
        }
      });
  if (!ok) {
    return QImage();
  }
  return image;
}  // TiffReader::extractNumericImage

QImage TiffReader::readRgbaImage(const TiffHandle& tif, const TiffInfo& info) {
  QImage image(info.width, info.height, info.samplesPerPixel == 3 ? QImage::Format_RGB32 : QImage::Format_ARGB32);
  if (image.isNull()) {
    throw std::bad_alloc();
  }

  // For ABGR -> ARGB conversion.
  TiffBuffer<uint32_t> tmpBuffer;
  const uint32_t* srcLine = nullptr;

  if (image.bytesPerLine() == 4 * info.width) {
    // We can avoid creating a temporary buffer in this case.
    if (!TIFFReadRGBAImageOriented(tif.handle(), info.width, info.height, (uint32_t*) image.bits(), ORIENTATION_TOPLEFT,
                                   0)) {
      return QImage();
    }
    srcLine = (const uint32_t*) image.bits();
  } else {
    // The multiplication is done in size_t to avoid an int overflow, which
    // would size the buffer far too small for what TIFFReadRGBAImageOriented()
    // is about to write into it.
    TiffBuffer<uint32_t>(static_cast<size_t>(info.width) * static_cast<size_t>(info.height)).swap(tmpBuffer);
    if (!TIFFReadRGBAImageOriented(tif.handle(), info.width, info.height, tmpBuffer.data(), ORIENTATION_TOPLEFT, 0)) {
      return QImage();
    }
    srcLine = tmpBuffer.data();
  }

  auto* dstLine = (uint32_t*) image.bits();
  assert(image.bytesPerLine() % 4 == 0);
  const int dstStride = image.bytesPerLine() / 4;
  for (int y = 0; y < info.height; ++y) {
    convertAbgrToArgb(srcLine, dstLine, info.width);
    srcLine += info.width;
    dstLine += dstStride;
  }
  return image;
}  // TiffReader::readRgbaImage
