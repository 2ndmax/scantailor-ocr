// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DownscaledImage.h"

#include <Transform.h>

#include <QTransform>
#include <cstdint>

#include "Dpi.h"
#include "Dpm.h"

using namespace imageproc;

QImage createDownscaledImage(const QImage& image) {
  if (image.isNull()) {
    return image;
  }

  // Original and downscaled DPM.
  const Dpm oDpm(image);
  const Dpm dDpm(Dpi(200, 200));

  const int oW = image.width();
  const int oH = image.height();

  if ((oW <= 0) || (oH <= 0) || (oDpm.horizontal() <= 0) || (oDpm.vertical() <= 0)) {
    // Empty image or unknown resolution: no downscaling, rather than dividing by zero.
    return image;
  }

  const int64_t scaledW = int64_t(oW) * dDpm.horizontal() / oDpm.horizontal();
  const int64_t scaledH = int64_t(oH) * dDpm.vertical() / oDpm.vertical();
  const int dW = static_cast<int>(qBound(int64_t(1), scaledW, int64_t(oW)));
  const int dH = static_cast<int>(qBound(int64_t(1), scaledH, int64_t(oH)));

  if ((dW * 1.2 > oW) || (dH * 1.2 > oH)) {
    // Sizes are close - no point in downscaling.
    return image;
  }

  QTransform xform;
  xform.scale((double) dW / oW, (double) dH / oH);
  return transform(image, xform, QRect(0, 0, dW, dH), OutsidePixels::assumeColor(Qt::white));
}
