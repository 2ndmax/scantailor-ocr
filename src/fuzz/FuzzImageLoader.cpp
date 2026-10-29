// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F4: decoding the pixels of an image file (TIFF, JPEG, PNG, JPEG 2000, and what Qt reads), as
// done for every processing step and thumbnail. Seeds: corpus/images, as for F1.

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <algorithm>
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

QImage load(QBuffer& buffer, const int pageNum, const QSize& minSize = QSize()) {
  buffer.seek(0);
  ImageLoadErrorCapture errors;
  QImage image = ImageLoader::load(buffer, pageNum, minSize);
  (void) errors.messages();
  return image;
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  // The same bound for the formats only Qt reads (no metadata loader, so not checked below), in MB.
  QImageReader::setAllocationLimit(static_cast<int>(MAX_FUZZ_PIXELS * 4 / (1024 * 1024)) + 1);

  QByteArray input = fuzz::bytes(data, size);
  QBuffer buffer(&input);
  buffer.open(QIODevice::ReadOnly);

  std::vector<QSize> pageSizes;
  {
    ImageLoadErrorCapture errors;
    ImageMetadataLoader::load(buffer, [&](const ImageMetadata& page) { pageSizes.push_back(page.size()); });
  }
  for (const QSize& pageSize : pageSizes) {
    if (qint64(pageSize.width()) * pageSize.height() > MAX_FUZZ_PIXELS) {
      return 0;
    }
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
