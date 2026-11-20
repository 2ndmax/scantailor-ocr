// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PictureDetection.h"

#include <GrayRasterOp.h>
#include <Grayscale.h>
#include <Morphology.h>
#include <SeedFill.h>

#include "DebugImages.h"
#include "TaskStatus.h"

using namespace imageproc;

namespace {
struct CombineInverted {
  static uint8_t transform(uint8_t src, uint8_t dst) {
    const unsigned dilated = dst;
    const unsigned eroded = src;
    const unsigned res = 255 - (255 - dilated) * eroded / 255;
    return static_cast<uint8_t>(res);
  }
};
}  // namespace

GrayImage detectPictures(const GrayImage& input300dpi,
                         const bool higherSearchSensitivity,
                         const TaskStatus& status,
                         DebugImages* dbg) {
  // We stretch the range of gray levels to cover the whole
  // range of [0, 255]. We do it because we want text
  // and background to be equally far from the center
  // of the whole range. Otherwise text printed with a big
  // font will be considered a picture.
  GrayImage stretched(stretchGrayRange(input300dpi, 0.01, 0.01));
  if (dbg) {
    dbg->add(stretched, "stretched");
  }

  status.throwIfCancelled();

  GrayImage eroded(erodeGray(stretched, QSize(3, 3), 0x00));
  if (dbg) {
    dbg->add(eroded, "eroded");
  }

  status.throwIfCancelled();

  GrayImage dilated(dilateGray(stretched, QSize(3, 3), 0xff));
  if (dbg) {
    dbg->add(dilated, "dilated");
  }

  stretched = GrayImage();  // Save memory.
  status.throwIfCancelled();

  grayRasterOp<CombineInverted>(dilated, eroded);
  GrayImage grayGradient(dilated);
  dilated = GrayImage();
  eroded = GrayImage();
  if (dbg) {
    dbg->add(grayGradient, "grayGradient");
  }

  status.throwIfCancelled();

  GrayImage marker(erodeGray(grayGradient, QSize(35, 35), 0x00));
  if (dbg) {
    dbg->add(marker, "marker");
  }

  status.throwIfCancelled();

  seedFillGrayInPlace(marker, grayGradient, CONN8);
  GrayImage reconstructed(marker);
  marker = GrayImage();
  if (dbg) {
    dbg->add(reconstructed, "reconstructed");
  }

  status.throwIfCancelled();

  grayRasterOp<GRopInvert<GRopSrc>>(reconstructed, reconstructed);
  if (dbg) {
    dbg->add(reconstructed, "reconstructed_inverted");
  }

  status.throwIfCancelled();

  GrayImage holesFilled(createFramedImage(reconstructed.size()));
  seedFillGrayInPlace(holesFilled, reconstructed, CONN8);
  reconstructed = GrayImage();
  if (dbg) {
    dbg->add(holesFilled, "holesFilled");
  }

  if (higherSearchSensitivity) {
    GrayImage stretched2(stretchGrayRange(holesFilled, 5.0, 0.01));
    if (dbg) {
      dbg->add(stretched2, "stretched2");
    }
    return stretched2;
  }
  return holesFilled;
}
