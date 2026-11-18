// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DownscaledImage.h"

#include <Transform.h>

#include <QTransform>
#include <cassert>

#include "Dpi.h"
#include "Dpm.h"

using namespace imageproc;

QImage createDownscaledImage(const QImage& image) {
  assert(!image.isNull());

  // Original and downscaled DPM.
  const Dpm oDpm(image);
  const Dpm dDpm(Dpi(200, 200));

  const int oW = image.width();
  const int oH = image.height();

  if ((oDpm.horizontal() <= 0) || (oDpm.vertical() <= 0)) {
    // Unknown resolution: no downscaling, rather than dividing by zero.
    return image;
  }

  int dW = oW * dDpm.horizontal() / oDpm.horizontal();
  int dH = oH * dDpm.vertical() / oDpm.vertical();
  dW = qBound(1, dW, oW);
  dH = qBound(1, dH, oH);

  if ((dW * 1.2 > oW) || (dH * 1.2 > oH)) {
    // Sizes are close - no point in downscaling.
    return image;
  }

  QTransform xform;
  xform.scale((double) dW / oW, (double) dH / oH);
  return transform(image, xform, QRect(0, 0, dW, dH), OutsidePixels::assumeColor(Qt::white));
}
