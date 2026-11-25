// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F4: decoding the pixels of an image file (TIFF, JPEG, PNG, JPEG 2000, and what Qt reads), as
// done for every processing step and thumbnail. Seeds: corpus/images, as for F1.

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <vector>

#include "FuzzSetup.h"
#include "ImageLoadErrors.h"
#include "ImageLoader.h"
#include "ImageMetadata.h"
#include "ImageMetadataLoader.h"

namespace {
/**
 * Pages larger than this are not decoded. A few bytes can claim a page up to the size limit of
 * the program (ImageSizeLimit), whose decoding takes gigabytes and seconds; the fuzzer would
 * spend its time there. Such sizes are covered by the tests of the size limit.
 */
constexpr qint64 MAX_FUZZ_PIXELS = 16 * 1000 * 1000;

uint16_t readBe16(const uint8_t* p) {
  return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

uint32_t readBe32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) | (static_cast<uint32_t>(p[2]) << 8)
         | p[3];
}

uint64_t readBe64(const uint8_t* p) {
  return (static_cast<uint64_t>(readBe32(p)) << 32) | readBe32(p + 4);
}

/**
 * Checks JP2/J2K codestream SIZ dimensions against MAX_FUZZ_PIXELS so a small JP2 ihdr box paired
 * with a multi-hundred-megapixel jp2c codestream cannot bypass the fuzzer pixel cap.
 */
bool jp2CodestreamExceedsFuzzLimit(const QByteArray& input) {
  static constexpr uint8_t JP2_SIGNATURE[] = {0x00, 0x00, 0x00, 0x0c, 0x6a, 0x50, 0x20, 0x20, 0x0d, 0x0a, 0x87, 0x0a};
  static constexpr uint8_t J2K_SIGNATURE[] = {0xff, 0x4f, 0xff, 0x51};

  const auto* bytes = reinterpret_cast<const uint8_t*>(input.constData());
  const qint64 size = input.size();

  qint64 codestreamPos = -1;
  if ((size >= static_cast<qint64>(sizeof(J2K_SIGNATURE)))
      && (std::memcmp(bytes, J2K_SIGNATURE, sizeof(J2K_SIGNATURE)) == 0)) {
    codestreamPos = 0;
  } else if ((size >= static_cast<qint64>(sizeof(JP2_SIGNATURE)))
             && (std::memcmp(bytes, JP2_SIGNATURE, sizeof(JP2_SIGNATURE)) == 0)) {
    qint64 pos = 0;
    while (pos + 8 <= size) {
      const uint32_t boxLen32 = readBe32(bytes + pos);
      const uint32_t boxType = readBe32(bytes + pos + 4);
      qint64 headerLen = 8;
      qint64 boxLen = boxLen32;
      if (boxLen32 == 0) {
        boxLen = size - pos;
      } else if (boxLen32 == 1) {
        if (pos + 16 > size) {
          break;
        }
        const uint64_t boxLen64 = readBe64(bytes + pos + 8);
        if ((boxLen64 < 16) || (boxLen64 > static_cast<uint64_t>(size - pos))) {
          break;
        }
        headerLen = 16;
        boxLen = static_cast<qint64>(boxLen64);
      } else if ((boxLen < 8) || (pos + boxLen > size)) {
        break;
      }
      if (boxType == 0x6a703263U) {  // "jp2c"
        codestreamPos = pos + headerLen;
        break;
      }
      pos += boxLen;
    }
  }

  if ((codestreamPos < 0) || (codestreamPos + 24 > size)) {
    return false;
  }
  const uint8_t* cs = bytes + codestreamPos;
  if (std::memcmp(cs, J2K_SIGNATURE, sizeof(J2K_SIGNATURE)) != 0) {
    return false;
  }

  const uint32_t xSiz = readBe32(cs + 8);
  const uint32_t ySiz = readBe32(cs + 12);
  const uint32_t xOffset = readBe32(cs + 16);
  const uint32_t yOffset = readBe32(cs + 20);
  if ((xSiz <= xOffset) || (ySiz <= yOffset)) {
    return false;
  }
  const int64_t width = static_cast<int64_t>(xSiz) - xOffset;
  const int64_t height = static_cast<int64_t>(ySiz) - yOffset;
  if ((width > MAX_FUZZ_PIXELS) || (height > MAX_FUZZ_PIXELS) || (width > MAX_FUZZ_PIXELS / height)) {
    return true;
  }

  if (codestreamPos + 42 > size) {
    return false;
  }
  const uint64_t tileWidth = readBe32(cs + 24);
  const uint64_t tileHeight = readBe32(cs + 28);
  const uint32_t tileXOffset = readBe32(cs + 32);
  const uint32_t tileYOffset = readBe32(cs + 36);
  const uint32_t numComps = readBe16(cs + 40);

  const int64_t numPixels = width * height;
  if ((numComps > 0) && (numPixels > (MAX_FUZZ_PIXELS * 4) / static_cast<int64_t>(numComps))) {
    return true;
  }
  if ((tileWidth > 0) && (tileHeight > 0) && (tileXOffset < xSiz) && (tileYOffset < ySiz)) {
    const uint64_t tilesX = (static_cast<uint64_t>(xSiz) - tileXOffset + tileWidth - 1) / tileWidth;
    const uint64_t tilesY = (static_cast<uint64_t>(ySiz) - tileYOffset + tileHeight - 1) / tileHeight;
    const uint64_t maxTiles
        = static_cast<uint64_t>((width + 31) / 32 + 1) * static_cast<uint64_t>((height + 31) / 32 + 1);
    if ((tilesX > maxTiles) || (tilesY > maxTiles) || (tilesX > maxTiles / tilesY)) {
      return true;
    }
  }
  return false;
}

QImage load(QBuffer& buffer, const int pageNum, const QSize& minSize = QSize()) {
  buffer.seek(0);
  ImageLoadErrorCapture errors;
  try {
    QImage image = ImageLoader::load(buffer, pageNum, minSize);
    (void) errors.messages();
    return image;
  } catch (const std::bad_alloc&) {
    return QImage();
  }
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  // The same bound for the formats only Qt reads (no metadata loader, so not checked below), in MB.
  QImageReader::setAllocationLimit(static_cast<int>(MAX_FUZZ_PIXELS * 4 / (1024 * 1024)) + 1);

  QByteArray input = fuzz::bytes(data, size);
  QBuffer buffer(&input);
  buffer.open(QIODevice::ReadOnly);

  std::vector<QSize> pageSizes;
  ImageMetadataLoader::Status status = ImageMetadataLoader::FORMAT_NOT_RECOGNIZED;
  try {
    ImageLoadErrorCapture errors;
    status = ImageMetadataLoader::load(buffer, [&](const ImageMetadata& page) { pageSizes.push_back(page.size()); });
  } catch (const std::bad_alloc&) {
    return 0;
  }
  // A recognized container whose metadata failed (e.g. a later damaged/huge TIFF page leaving
  // pageSizes empty) must not fall through to load(buffer, 0) without a size check.
  if ((status != ImageMetadataLoader::LOADED) && (status != ImageMetadataLoader::FORMAT_NOT_RECOGNIZED)) {
    return 0;
  }
  for (const QSize& pageSize : pageSizes) {
    if (qint64(pageSize.width()) * pageSize.height() > MAX_FUZZ_PIXELS) {
      return 0;
    }
  }
  if (jp2CodestreamExceedsFuzzLimit(input)) {
    return 0;
  }

  // The first two pages in full, the first one also as a thumbnail (reduced resolution for JPEG
  // and JPEG 2000), and a page number past the last page.
  const int numPages = static_cast<int>(pageSizes.size());
  for (int pageNum = 0; pageNum < std::min(numPages, 2); ++pageNum) {
    (void) load(buffer, pageNum).bits();
  }
  if (numPages > 0) {
    (void) load(buffer, 0, QSize(8, 8)).bits();
  }
  (void) load(buffer, numPages);
  return 0;
}
