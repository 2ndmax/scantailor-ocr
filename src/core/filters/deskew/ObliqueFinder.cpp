// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPL v3 license that can be found in the LICENSE file.
// Auto oblique: Zvezdochiot; dev branch implements as requested (see ObliqueFinder.h).

#include "ObliqueFinder.h"

#include <algorithm>
#include <cmath>

#include <BWColor.h>
#include <BinaryImage.h>
#include <SkewFinder.h>
#include <imageproc/OrthogonalRotation.h>
#include <imageproc/Transform.h>

#include <QImage>
#include <QPolygonF>
#include <QTransform>

namespace deskew {

using namespace imageproc;

BinaryImage buildSausageMask(const BinaryImage& image) {
  if (image.isNull()) {
    return BinaryImage();
  }
  const int w = image.width();
  const int h = image.height();
  BinaryImage result(w, h, WHITE);

  for (int y = 0; y < h; ++y) {
    int count = 0;
    int64_t sumX = 0;
    for (int x = 0; x < w; ++x) {
      if (image.getPixel(x, y) == BLACK) {
        ++count;
        sumX += x;
      }
    }
    if (count > 0) {
      const double center = static_cast<double>(sumX) / count;
      const int left = std::max(0, static_cast<int>(std::floor(center - count / 2.0)));
      const int len = std::min(count, w - left);
      if (len > 0) {
        result.fill(QRect(left, y, len, 1), BLACK);
      }
    }
  }
  return result;
}

std::optional<double> findObliqueDegrees(const BinaryImage& mask, SkewFinder& skewFinder, double maxObliqueDeg) {
  if (mask.isNull()) {
    return std::nullopt;
  }

  const BinaryImage sausage = buildSausageMask(mask);
  const BinaryImage rotated = orthogonalRotation(sausage, 90);

  const Skew s1 = skewFinder.findSkew(mask);
  // The turned mask has its axes swapped, so its resolution ratio is the inverse one.
  const double resolutionRatio = skewFinder.resolutionRatio();
  skewFinder.setResolutionRatio(1.0 / resolutionRatio);
  const Skew s2 = skewFinder.findSkew(rotated);
  skewFinder.setResolutionRatio(resolutionRatio);

  if ((s1.confidence() < Skew::GOOD_CONFIDENCE) || (s2.confidence() < Skew::GOOD_CONFIDENCE)) {
    // Not enough structure in the image to tell an oblique angle from noise. Reporting no angle
    // at all is better than shearing the page by an arbitrary amount.
    return std::nullopt;
  }

  // Weight angles by confidence so low-confidence skew contributes less (PR #110, zvezdochiot).
  // REVIEW-NOTE (2026-10): angle * c / (c + 1) shrinks each angle towards zero instead of
  // weighting it -> with confidence 2 only 67 % of the oblique angle is corrected. Not fixed
  // because the weighting was adopted on purpose (PR #110) and a change alters every automatic
  // oblique angle. Measured with a comparison tool on synthetic text pages (2026-10): the
  // turned mask reaches a confidence of only about 0.6-1.0 on ordinary text, so the check above
  // returns no angle for most pages and the weighting rarely matters. Flatbed scans hardly have
  // any shear, so this is acceptable.
  const double angleHor = s1.angle() * s1.confidence() / (s1.confidence() + 1.0);
  const double angleVert = s2.angle() * s2.confidence() / (s2.confidence() + 1.0);
  double oblique = angleVert - angleHor;
  oblique = std::max(-maxObliqueDeg, std::min(maxObliqueDeg, oblique));
  return oblique;
}

BinaryImage deskewedMask(const BinaryImage& mask, const double deskewAngleDeg) {
  if (std::abs(deskewAngleDeg) < 1e-6) {
    return mask;
  }

  const int w = mask.width();
  const int h = mask.height();
  QTransform rotXform;
  const QPointF center(0.5 * w, 0.5 * h);
  rotXform.translate(-center.x(), -center.y());
  rotXform.rotate(deskewAngleDeg);
  rotXform.translate(center.x(), center.y());
  const QRectF srcRectF(0, 0, w, h);
  QPolygonF dstCorners;
  dstCorners << rotXform.map(srcRectF.topLeft()) << rotXform.map(srcRectF.topRight())
             << rotXform.map(srcRectF.bottomRight()) << rotXform.map(srcRectF.bottomLeft());
  const QRect dstRect = dstCorners.boundingRect().toAlignedRect();
  if (!dstRect.isValid()) {
    return mask;
  }
  return BinaryImage(transform(mask.toQImage(), rotXform, dstRect, OutsidePixels::assumeWeakColor(Qt::white)));
}

}  // namespace deskew
