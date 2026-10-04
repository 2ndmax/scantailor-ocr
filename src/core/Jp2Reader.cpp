// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Jp2Reader.h"

#include <openjpeg.h>

#include <QCoreApplication>
#include <QIODevice>
#include <QImage>
#include <QThread>
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

#include "Dpm.h"
#include "ImageLoadErrors.h"
#include "ImageMetadata.h"

namespace {
enum class Jp2Format { NONE, JP2, J2K };

// The JP2 signature box, shared by JP2, JPX (JPF) and JPH files.
const unsigned char JP2_SIGNATURE[12] = {0x00, 0x00, 0x00, 0x0C, 0x6A, 0x50, 0x20, 0x20, 0x0D, 0x0A, 0x87, 0x0A};
// SOC followed by SIZ: a raw codestream (J2K, J2C, JPC, JHC).
const unsigned char J2K_SIGNATURE[4] = {0xFF, 0x4F, 0xFF, 0x51};

Jp2Format detectFormat(QIODevice& device) {
  if (!device.isReadable() || device.isSequential()) {
    return Jp2Format::NONE;
  }
  unsigned char data[sizeof(JP2_SIGNATURE)];
  const qint64 numRead = device.peek(reinterpret_cast<char*>(data), sizeof(data));
  if ((numRead >= static_cast<qint64>(sizeof(JP2_SIGNATURE)))
      && (std::memcmp(data, JP2_SIGNATURE, sizeof(JP2_SIGNATURE)) == 0)) {
    return Jp2Format::JP2;
  }
  if ((numRead >= static_cast<qint64>(sizeof(J2K_SIGNATURE)))
      && (std::memcmp(data, J2K_SIGNATURE, sizeof(J2K_SIGNATURE)) == 0)) {
    return Jp2Format::J2K;
  }
  return Jp2Format::NONE;
}

/*============================== Header parsing ==============================*/

uint16_t readBe16(const unsigned char* p) {
  return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

uint32_t readBe32(const unsigned char* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8)
         | static_cast<uint32_t>(p[3]);
}

uint64_t readBe64(const unsigned char* p) {
  return (static_cast<uint64_t>(readBe32(p)) << 32) | readBe32(p + 4);
}

constexpr uint32_t boxType(const char (&name)[5]) {
  return (static_cast<uint32_t>(static_cast<unsigned char>(name[0])) << 24)
         | (static_cast<uint32_t>(static_cast<unsigned char>(name[1])) << 16)
         | (static_cast<uint32_t>(static_cast<unsigned char>(name[2])) << 8)
         | static_cast<uint32_t>(static_cast<unsigned char>(name[3]));
}

bool readAt(QIODevice& device, const qint64 pos, unsigned char* buf, const qint64 len) {
  return device.seek(pos) && (device.read(reinterpret_cast<char*>(buf), len) == len);
}

struct Box {
  uint32_t type = 0;
  qint64 dataPos = 0;
  qint64 dataEnd = 0;
};

/**
 * Reads the header of the box starting at \p pos, which has to end
 * no later than \p limit.
 */
bool readBox(QIODevice& device, const qint64 pos, const qint64 limit, Box& box) {
  unsigned char header[16];
  if ((pos + 8 > limit) || !readAt(device, pos, header, 8)) {
    return false;
  }
  uint64_t length = readBe32(header);
  box.type = readBe32(header + 4);
  qint64 headerLength = 8;
  if (length == 1) {
    // 64-bit extended length.
    if ((pos + 16 > limit) || !readAt(device, pos + 8, header + 8, 8)) {
      return false;
    }
    length = readBe64(header + 8);
    headerLength = 16;
  } else if (length == 0) {
    // The box extends to the end of its container.
    length = static_cast<uint64_t>(limit - pos);
  }
  if (length < static_cast<uint64_t>(headerLength)) {
    return false;
  }
  // Be lenient with boxes claiming to extend past their container.
  const qint64 end = (length > static_cast<uint64_t>(limit - pos)) ? limit : pos + static_cast<qint64>(length);
  box.dataPos = pos + headerLength;
  box.dataEnd = end;
  return true;
}

/**
 * Parses a 'resc' or 'resd' box: the resolution in grid points per meter,
 * each coordinate stored as numerator, denominator and a power of ten.
 */
Dpm parseResolution(const unsigned char* data) {
  const uint16_t vNum = readBe16(data);
  const uint16_t vDen = readBe16(data + 2);
  const uint16_t hNum = readBe16(data + 4);
  const uint16_t hDen = readBe16(data + 6);
  const auto vExp = static_cast<int8_t>(data[8]);
  const auto hExp = static_cast<int8_t>(data[9]);
  if ((vNum == 0) || (vDen == 0) || (hNum == 0) || (hDen == 0)) {
    return Dpm();
  }
  const double vertical = double(vNum) / vDen * std::pow(10.0, vExp);
  const double horizontal = double(hNum) / hDen * std::pow(10.0, hExp);
  if (!(vertical >= 1.0 && vertical < INT_MAX) || !(horizontal >= 1.0 && horizontal < INT_MAX)) {
    return Dpm();
  }
  return Dpm(static_cast<int>(std::lround(horizontal)), static_cast<int>(std::lround(vertical)));
}

struct HeaderInfo {
  QSize size;
  Dpm dpm;  // Null if not specified.
};

bool parseJp2Header(QIODevice& device, HeaderInfo& info) {
  const qint64 fileSize = device.size();
  Box box;
  for (qint64 pos = 0; readBox(device, pos, fileSize, box); pos = box.dataEnd) {
    if (box.type == boxType("jp2c")) {
      // The header box has to come before the codestream.
      break;
    }
    if (box.type != boxType("jp2h")) {
      continue;
    }

    Dpm captureDpm;
    Dpm displayDpm;
    Box child;
    for (qint64 childPos = box.dataPos; readBox(device, childPos, box.dataEnd, child); childPos = child.dataEnd) {
      if (child.type == boxType("ihdr")) {
        unsigned char data[8];
        if ((child.dataEnd - child.dataPos >= 8) && readAt(device, child.dataPos, data, 8)) {
          const uint32_t height = readBe32(data);
          const uint32_t width = readBe32(data + 4);
          if ((width > 0) && (height > 0) && (width <= INT_MAX) && (height <= INT_MAX)) {
            info.size = QSize(static_cast<int>(width), static_cast<int>(height));
          }
        }
      } else if (child.type == boxType("res ")) {
        Box resBox;
        for (qint64 resPos = child.dataPos; readBox(device, resPos, child.dataEnd, resBox); resPos = resBox.dataEnd) {
          unsigned char data[10];
          if ((resBox.dataEnd - resBox.dataPos < 10) || !readAt(device, resBox.dataPos, data, 10)) {
            continue;
          }
          if (resBox.type == boxType("resc")) {
            captureDpm = parseResolution(data);
          } else if (resBox.type == boxType("resd")) {
            displayDpm = parseResolution(data);
          }
        }
      }
    }
    // The capture resolution is what a scanner writes.
    info.dpm = !captureDpm.isNull() ? captureDpm : displayDpm;
    return !info.size.isEmpty();
  }
  return false;
}

bool parseJ2kHeader(QIODevice& device, HeaderInfo& info) {
  // SOC (2), SIZ marker (2), Lsiz (2), Rsiz (2), Xsiz (4), Ysiz (4), XOsiz (4), YOsiz (4)
  unsigned char data[24];
  if (!readAt(device, 0, data, sizeof(data))) {
    return false;
  }
  const uint32_t xSiz = readBe32(data + 8);
  const uint32_t ySiz = readBe32(data + 12);
  const uint32_t xOffset = readBe32(data + 16);
  const uint32_t yOffset = readBe32(data + 20);
  if ((xSiz <= xOffset) || (ySiz <= yOffset) || (xSiz - xOffset > INT_MAX) || (ySiz - yOffset > INT_MAX)) {
    return false;
  }
  info.size = QSize(static_cast<int>(xSiz - xOffset), static_cast<int>(ySiz - yOffset));
  return true;
}

bool parseHeader(QIODevice& device, const Jp2Format format, HeaderInfo& info) {
  const qint64 origPos = device.pos();
  const bool ok = (format == Jp2Format::JP2) ? parseJp2Header(device, info) : parseJ2kHeader(device, info);
  device.seek(origPos);
  return ok;
}

/*================================ OpenJPEG ==================================*/

void opjErrorHandler(const char* msg, void*) {
  ImageLoadErrorCapture::addError(QLatin1String("OpenJPEG: ") + QString::fromUtf8(msg));
}

void opjWarningHandler(const char* msg, void*) {
  ImageLoadErrorCapture::addWarning(QLatin1String("OpenJPEG: ") + QString::fromUtf8(msg));
}

OPJ_SIZE_T streamRead(void* buffer, OPJ_SIZE_T numBytes, void* userData) {
  auto* device = static_cast<QIODevice*>(userData);
  const qint64 numRead = device->read(static_cast<char*>(buffer), static_cast<qint64>(numBytes));
  return (numRead > 0) ? static_cast<OPJ_SIZE_T>(numRead) : static_cast<OPJ_SIZE_T>(-1);
}

OPJ_OFF_T streamSkip(OPJ_OFF_T numBytes, void* userData) {
  auto* device = static_cast<QIODevice*>(userData);
  const qint64 target = std::min<qint64>(device->pos() + numBytes, device->size());
  if ((target < 0) || !device->seek(target)) {
    return -1;
  }
  return numBytes;
}

OPJ_BOOL streamSeek(OPJ_OFF_T pos, void* userData) {
  auto* device = static_cast<QIODevice*>(userData);
  return device->seek(pos) ? OPJ_TRUE : OPJ_FALSE;
}

struct StreamDeleter {
  void operator()(opj_stream_t* stream) const { opj_stream_destroy(stream); }
};

struct CodecDeleter {
  void operator()(opj_codec_t* codec) const { opj_destroy_codec(codec); }
};

struct ImageDeleter {
  void operator()(opj_image_t* image) const { opj_image_destroy(image); }
};

using StreamPtr = std::unique_ptr<opj_stream_t, StreamDeleter>;
using CodecPtr = std::unique_ptr<opj_codec_t, CodecDeleter>;
using ImagePtr = std::unique_ptr<opj_image_t, ImageDeleter>;

/**
 * Reads one component of the decoded image, scaled to 8 bits
 * and resampled (nearest neighbour) to the output size.
 */
class Channel {
 public:
  Channel(const opj_image_comp_t& comp, const int outWidth, const int outHeight)
      : m_data(comp.data),
        m_width(static_cast<int>(comp.w)),
        m_height(static_cast<int>(comp.h)),
        m_outHeight(outHeight) {
    const int prec = static_cast<int>(comp.prec);
    m_offset = comp.sgnd ? (1 << (prec - 1)) : 0;
    m_shift = std::max(0, prec - 8);
    m_maxValue = (1 << std::min(prec, 8)) - 1;

    m_columns.resize(outWidth);
    for (int x = 0; x < outWidth; ++x) {
      m_columns[x] = std::min(static_cast<int>(static_cast<int64_t>(x) * m_width / outWidth), m_width - 1);
    }
  }

  const OPJ_INT32* row(const int y) const {
    const int srcY = std::min(static_cast<int>(static_cast<int64_t>(y) * m_height / m_outHeight), m_height - 1);
    return m_data + static_cast<size_t>(srcY) * m_width;
  }

  int value(const OPJ_INT32* row, const int x) const {
    const int64_t shifted = (static_cast<int64_t>(row[m_columns[x]]) + m_offset) >> m_shift;
    const auto v = static_cast<int>(std::min<int64_t>(std::max<int64_t>(shifted, 0), m_maxValue));
    // Precisions below 8 bits are stretched to the full range.
    return (m_maxValue == 255) ? v : v * 255 / m_maxValue;
  }

 private:
  const OPJ_INT32* m_data;
  int m_width;
  int m_height;
  int m_outHeight;
  int m_offset;
  int m_shift;
  int m_maxValue;
  std::vector<int> m_columns;
};

int clampTo8Bit(const int v) {
  return std::min(std::max(v, 0), 255);
}

QImage toQImage(const opj_image_t& image) {
  const auto numComps = static_cast<int>(image.numcomps);
  if (numComps < 1) {
    return QImage();
  }
  for (int i = 0; i < numComps; ++i) {
    const opj_image_comp_t& comp = image.comps[i];
    if (!comp.data || (comp.w == 0) || (comp.h == 0) || (comp.w > INT_MAX) || (comp.h > INT_MAX) || (comp.prec < 1)
        || (comp.prec > 31)) {
      ImageLoadErrorCapture::addError(
          QCoreApplication::translate("Jp2Reader", "The image contains a component that could not be decoded."));
      return QImage();
    }
  }

  const opj_image_comp_t& first = image.comps[0];
  const uint64_t outWidth64 = static_cast<uint64_t>(first.w) * std::max<OPJ_UINT32>(first.dx, 1);
  const uint64_t outHeight64 = static_cast<uint64_t>(first.h) * std::max<OPJ_UINT32>(first.dy, 1);
  if ((outWidth64 > INT_MAX) || (outHeight64 > INT_MAX)) {
    throw std::bad_alloc();
  }
  const auto outWidth = static_cast<int>(outWidth64);
  const auto outHeight = static_cast<int>(outHeight64);

  enum ColorModel { GRAY, RGB, YCC, CMYK };
  ColorModel model = RGB;
  if ((numComps < 3) || (image.color_space == OPJ_CLRSPC_GRAY)) {
    model = GRAY;
  } else if ((image.color_space == OPJ_CLRSPC_SYCC) || (image.color_space == OPJ_CLRSPC_EYCC)) {
    model = YCC;
  } else if ((image.color_space == OPJ_CLRSPC_CMYK) && (numComps >= 4)) {
    model = CMYK;
  }
  const bool hasAlpha = (model == RGB) && (numComps >= 4) && (image.comps[3].alpha != 0);

  std::vector<Channel> channels;
  const int numChannels = (model == GRAY) ? 1 : ((model == CMYK) || hasAlpha) ? 4 : 3;
  channels.reserve(numChannels);
  for (int i = 0; i < numChannels; ++i) {
    channels.emplace_back(image.comps[i], outWidth, outHeight);
  }

  QImage::Format format = QImage::Format_RGB32;
  if (model == GRAY) {
    format = QImage::Format_Indexed8;
  } else if (hasAlpha) {
    format = QImage::Format_ARGB32;
  }
  QImage result(outWidth, outHeight, format);
  if (result.isNull()) {
    throw std::bad_alloc();
  }

  if (model == GRAY) {
    result.setColorCount(256);
    for (int i = 0; i < 256; ++i) {
      result.setColor(i, qRgb(i, i, i));
    }
    for (int y = 0; y < outHeight; ++y) {
      const OPJ_INT32* src = channels[0].row(y);
      uint8_t* dst = result.scanLine(y);
      for (int x = 0; x < outWidth; ++x) {
        dst[x] = static_cast<uint8_t>(channels[0].value(src, x));
      }
    }
    return result;
  }

  std::vector<const OPJ_INT32*> rows(numChannels);
  for (int y = 0; y < outHeight; ++y) {
    for (int c = 0; c < numChannels; ++c) {
      rows[c] = channels[c].row(y);
    }
    auto* dst = reinterpret_cast<QRgb*>(result.scanLine(y));
    for (int x = 0; x < outWidth; ++x) {
      const int v0 = channels[0].value(rows[0], x);
      const int v1 = channels[1].value(rows[1], x);
      const int v2 = channels[2].value(rows[2], x);
      switch (model) {
        case YCC: {
          // ITU-R BT.601 (JFIF) YCbCr to RGB, in 16.16 fixed point.
          const int cb = v1 - 128;
          const int cr = v2 - 128;
          const int r = v0 + ((91881 * cr + 32768) >> 16);
          const int g = v0 - ((22554 * cb + 46802 * cr + 32768) >> 16);
          const int b = v0 + ((116130 * cb + 32768) >> 16);
          dst[x] = qRgb(clampTo8Bit(r), clampTo8Bit(g), clampTo8Bit(b));
          break;
        }
        case CMYK: {
          const int k = 255 - channels[3].value(rows[3], x);
          dst[x] = qRgb((255 - v0) * k / 255, (255 - v1) * k / 255, (255 - v2) * k / 255);
          break;
        }
        default:
          if (hasAlpha) {
            dst[x] = qRgba(v0, v1, v2, channels[3].value(rows[3], x));
          } else {
            dst[x] = qRgb(v0, v1, v2);
          }
          break;
      }
    }
  }
  return result;
}  // toQImage

/**
 * The number of resolution levels that can be discarded when decoding.
 */
int maxReduceFactor(opj_codec_t* codec, const int numComps) {
  opj_codestream_info_v2_t* info = opj_get_cstr_info(codec);
  if (!info) {
    return 0;
  }
  int minResolutions = INT_MAX;
  if (info->m_default_tile_info.tccp_info) {
    for (int i = 0; i < numComps; ++i) {
      minResolutions = std::min<int>(minResolutions, info->m_default_tile_info.tccp_info[i].numresolutions);
    }
  }
  opj_destroy_cstr_info(&info);
  return (minResolutions == INT_MAX) ? 0 : std::max(0, minResolutions - 1);
}

// OpenJPEG keeps every decoded sample as a 32-bit integer.  Images needing more
// than this for a full decode are decoded in horizontal strips instead.  Strips
// cost some speed (code-blocks at strip boundaries are decoded twice), so only
// really huge images (e.g. large maps or 1200 DPI scans of big originals) take
// that path, while ordinary scans are decoded in one go.
std::atomic<uint64_t> g_stripDecodingThreshold{uint64_t(1) << 30};  // 1 GiB
// The sample memory aimed at per strip.
std::atomic<uint64_t> g_stripBytes{uint64_t(128) << 20};  // 128 MiB
// Strips are at least this high, to limit the overhead at strip boundaries.
std::atomic<uint32_t> g_minStripRows{256};

bool isSingleTile(opj_codec_t* codec) {
  opj_codestream_info_v2_t* info = opj_get_cstr_info(codec);
  if (!info) {
    return false;
  }
  const bool singleTile = (info->tw == 1) && (info->th == 1);
  opj_destroy_cstr_info(&info);
  return singleTile;
}

bool hasSubsampledComponents(const opj_image_t& image) {
  for (OPJ_UINT32 i = 0; i < image.numcomps; ++i) {
    if ((image.comps[i].dx != 1) || (image.comps[i].dy != 1)) {
      return true;
    }
  }
  return false;
}

/**
 * \brief Decodes a single-tiled image strip by strip.
 *
 * OpenJPEG (2.3+) allows repeated opj_set_decode_area() / opj_decode() calls
 * for single-tiled images, allocating memory for the requested area only.
 *
 * \param fallbackNeeded Set if the image turns out to be unsuitable for this
 *        (palettes and channel definitions are applied to each strip by OpenJPEG,
 *        which doesn't combine well with repeated decoding), or if anything fails.
 */
QImage decodeInStrips(opj_codec_t* codec, opj_stream_t* stream, opj_image_t* image, bool& fallbackNeeded) {
  fallbackNeeded = true;

  const OPJ_UINT32 x0 = image->x0;
  const OPJ_UINT32 y0 = image->y0;
  const OPJ_UINT32 x1 = image->x1;
  const OPJ_UINT32 y1 = image->y1;
  const OPJ_UINT32 numComps = image->numcomps;
  const uint64_t bytesPerRow = static_cast<uint64_t>(x1 - x0) * numComps * sizeof(OPJ_INT32);
  const auto rowsPerStrip = static_cast<OPJ_UINT32>(
      std::max<uint64_t>(g_minStripRows.load(), g_stripBytes.load() / std::max<uint64_t>(bytesPerRow, 1)));

  QImage result;
  for (OPJ_UINT32 stripY0 = y0; stripY0 < y1;) {
    const OPJ_UINT32 stripY1 = (y1 - stripY0 > rowsPerStrip) ? stripY0 + rowsPerStrip : y1;
    if (!opj_set_decode_area(codec, image, static_cast<OPJ_INT32>(x0), static_cast<OPJ_INT32>(stripY0),
                             static_cast<OPJ_INT32>(x1), static_cast<OPJ_INT32>(stripY1))
        || !opj_decode(codec, stream, image)) {
      return QImage();
    }
    if (image->numcomps != numComps) {
      // A palette was expanded into several components.
      return QImage();
    }
    for (OPJ_UINT32 i = 0; i < numComps; ++i) {
      if (image->comps[i].alpha) {
        // Channel definitions may reorder the components.
        return QImage();
      }
    }

    const QImage strip = toQImage(*image);
    if (strip.isNull()) {
      return QImage();
    }
    if (result.isNull()) {
      result = QImage(static_cast<int>(x1 - x0), static_cast<int>(y1 - y0), strip.format());
      if (result.isNull()) {
        throw std::bad_alloc();
      }
      result.setColorTable(strip.colorTable());
    }
    if ((strip.format() != result.format()) || (strip.width() != result.width())
        || (strip.height() != static_cast<int>(stripY1 - stripY0))) {
      return QImage();
    }

    const auto bytesToCopy = static_cast<size_t>(std::min(strip.bytesPerLine(), result.bytesPerLine()));
    for (int y = 0; y < strip.height(); ++y) {
      std::memcpy(result.scanLine(static_cast<int>(stripY0 - y0) + y), strip.constScanLine(y), bytesToCopy);
    }
    stripY0 = stripY1;
  }

  if (!opj_end_decompress(codec, stream)) {
    return QImage();
  }
  fallbackNeeded = false;
  return result;
}  // decodeInStrips

/**
 * \param allowStrips Whether huge images may be decoded in strips.
 * \param reduce Set to the number of discarded resolution levels.
 * \param stripsFailed Set if strip decoding was attempted and failed,
 *        in which case the caller should retry with allowStrips = false.
 */
QImage decode(QIODevice& device,
              const Jp2Format format,
              const QSize& minSize,
              const bool allowStrips,
              int& reduce,
              bool& stripsFailed) {
  reduce = 0;
  stripsFailed = false;
  if (!device.seek(0)) {
    return QImage();
  }

  StreamPtr stream(opj_stream_create(1 << 20, OPJ_TRUE));
  if (!stream) {
    return QImage();
  }
  opj_stream_set_user_data(stream.get(), &device, nullptr);
  opj_stream_set_user_data_length(stream.get(), static_cast<OPJ_UINT64>(device.size()));
  opj_stream_set_read_function(stream.get(), &streamRead);
  opj_stream_set_skip_function(stream.get(), &streamSkip);
  opj_stream_set_seek_function(stream.get(), &streamSeek);

  CodecPtr codec(opj_create_decompress(format == Jp2Format::JP2 ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K));
  if (!codec) {
    return QImage();
  }
  opj_set_error_handler(codec.get(), &opjErrorHandler, nullptr);
  opj_set_warning_handler(codec.get(), &opjWarningHandler, nullptr);

  opj_dparameters_t parameters;
  opj_set_default_decoder_parameters(&parameters);
  if (!opj_setup_decoder(codec.get(), &parameters)) {
    return QImage();
  }
  // Decoding is by far the most expensive part, and OpenJPEG parallelizes it well.
  // Failure just means OpenJPEG was built without thread support.
  opj_codec_set_threads(codec.get(), std::max(1, QThread::idealThreadCount()));

  opj_image_t* rawImage = nullptr;
  if (!opj_read_header(stream.get(), codec.get(), &rawImage)) {
    if (rawImage) {
      opj_image_destroy(rawImage);
    }
    return QImage();
  }
  ImagePtr image(rawImage);

  if (minSize.isValid() && !minSize.isEmpty()) {
    const int64_t fullWidth = static_cast<int64_t>(image->x1) - image->x0;
    const int64_t fullHeight = static_cast<int64_t>(image->y1) - image->y0;
    const int maxReduce = maxReduceFactor(codec.get(), static_cast<int>(image->numcomps));
    while ((reduce < maxReduce) && (((fullWidth + (int64_t(1) << (reduce + 1)) - 1) >> (reduce + 1)) >= minSize.width())
           && (((fullHeight + (int64_t(1) << (reduce + 1)) - 1) >> (reduce + 1)) >= minSize.height())) {
      ++reduce;
    }
    if ((reduce > 0) && !opj_set_decoded_resolution_factor(codec.get(), static_cast<OPJ_UINT32>(reduce))) {
      reduce = 0;
    }
  }

  if (allowStrips && (reduce == 0)) {
    const uint64_t fullDecodeBytes
        = static_cast<uint64_t>(image->x1 - image->x0) * (image->y1 - image->y0) * image->numcomps * sizeof(OPJ_INT32);
    if ((fullDecodeBytes > g_stripDecodingThreshold.load()) && !hasSubsampledComponents(*image)
        && isSingleTile(codec.get())) {
      QImage result = decodeInStrips(codec.get(), stream.get(), image.get(), stripsFailed);
      return result;
    }
  }

  if (!opj_decode(codec.get(), stream.get(), image.get()) || !opj_end_decompress(codec.get(), stream.get())) {
    return QImage();
  }

  return toQImage(*image);
}  // decode
}  // namespace

void Jp2Reader::setStripDecodingParameters(const uint64_t thresholdBytes,
                                           const uint64_t stripBytes,
                                           const uint32_t minStripRows) {
  g_stripDecodingThreshold = thresholdBytes;
  g_stripBytes = stripBytes;
  g_minStripRows = std::max<uint32_t>(minStripRows, 1);
}

bool Jp2Reader::canRead(QIODevice& device) {
  return detectFormat(device) != Jp2Format::NONE;
}

ImageMetadataLoader::Status Jp2Reader::readMetadata(QIODevice& device,
                                                    const VirtualFunction<void, const ImageMetadata&>& out) {
  const Jp2Format format = detectFormat(device);
  if (format == Jp2Format::NONE) {
    return ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;
  }

  HeaderInfo info;
  if (!parseHeader(device, format, info)) {
    ImageLoadErrorCapture::addError(
        QCoreApplication::translate("Jp2Reader", "The JPEG 2000 file header is damaged or incomplete."));
    return ImageMetadataLoader::GENERIC_ERROR;
  }

  out(ImageMetadata(info.size, Dpi(info.dpm)));
  return ImageMetadataLoader::LOADED;
}

QImage Jp2Reader::readImage(QIODevice& device, const QSize& minSize) {
  // The signature has to be checked at the beginning of the file,
  // wherever the device was left by a previous read.
  if (!device.seek(0)) {
    return QImage();
  }
  const Jp2Format format = detectFormat(device);
  if (format == Jp2Format::NONE) {
    return QImage();
  }

  HeaderInfo info;
  parseHeader(device, format, info);

  int reduce = 0;
  auto decodeWithFallback = [&](const QSize& min) {
    bool stripsFailed = false;
    QImage decoded = decode(device, format, min, true, reduce, stripsFailed);
    if (decoded.isNull() && stripsFailed) {
      // Strip decoding isn't possible for this file.  Try in one go.
      decoded = decode(device, format, min, false, reduce, stripsFailed);
    }
    return decoded;
  };

  QImage image = decodeWithFallback(minSize);
  if (image.isNull() && (reduce > 0)) {
    // Some files have tiles with fewer resolution levels than announced
    // in the main header.  Try again at full resolution.
    image = decodeWithFallback(QSize());
  }
  if (image.isNull()) {
    return QImage();
  }

  if (!info.dpm.isNull()) {
    image.setDotsPerMeterX(std::max(1, info.dpm.horizontal() >> reduce));
    image.setDotsPerMeterY(std::max(1, info.dpm.vertical() >> reduce));
  }
  return image;
}
