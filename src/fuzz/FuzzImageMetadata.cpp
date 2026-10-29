// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F1: reading the page sizes and resolutions of an image file (TIFF, JPEG, PNG, JPEG 2000), as
// done when files are added to a project. Seeds: corpus/images.

#include <QBuffer>

#include "FuzzSetup.h"
#include "ImageLoadErrors.h"
#include "ImageMetadata.h"
#include "ImageMetadataLoader.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  QByteArray input = fuzz::bytes(data, size);
  QBuffer buffer(&input);
  buffer.open(QIODevice::ReadOnly);

  ImageLoadErrorCapture errors;
  ImageMetadataLoader::load(buffer, [](const ImageMetadata& page) {
    (void) page.isDpiOK();
    (void) page.size();
  });
  (void) errors.messages();
  return 0;
}
