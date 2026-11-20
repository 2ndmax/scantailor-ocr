// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTIMAGEOPS_H_
#define SCANTAILOR_OUTPUT_OUTPUTIMAGEOPS_H_

#include <BinaryImage.h>

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QTransform>
#include <cstdint>

class Dpi;
class XSpline;

namespace output::detail {
struct RaiseAboveBackground {
  static uint8_t transform(uint8_t src, uint8_t dst);
};

void reserveBlackAndWhite(QImage& img, const imageproc::BinaryImage& mask);

void fillMarginsInPlace(QImage& image, const QPolygonF& contentPoly, const QColor& color, bool antialiasing = true);

void fillMarginsInPlace(imageproc::BinaryImage& image, const QPolygonF& contentPoly, imageproc::BWColor color);

void fillMarginsInPlace(imageproc::BinaryImage& image,
                        const imageproc::BinaryImage& contentMask,
                        imageproc::BWColor color);

void fillMarginsInPlace(QImage& image, const imageproc::BinaryImage& contentMask, const QColor& color);

void BinaryImageXOR(imageproc::BinaryImage& image, const imageproc::BinaryImage& bwMask, imageproc::BWColor color);

void applyAffineTransform(QImage& image, const QTransform& xform, const QColor& outsideColor);

void applyAffineTransform(imageproc::BinaryImage& image, const QTransform& xform, imageproc::BWColor outsideColor);

void movePointToTopMargin(imageproc::BinaryImage& bwImage, XSpline& spline, int idx);

void movePointToBottomMargin(imageproc::BinaryImage& bwImage, XSpline& spline, int idx);

void drawPoint(QImage& image, const QPointF& pt);

QImage smoothToGrayscale(const QImage& src, const Dpi& dpi);

QSize to300dpi(const QSize& size, const Dpi& sourceDpi);
}  // namespace output::detail

#endif  // SCANTAILOR_OUTPUT_OUTPUTIMAGEOPS_H_
