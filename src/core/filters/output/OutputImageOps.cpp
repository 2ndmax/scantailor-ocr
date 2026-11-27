// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputImageOps.h"

#include <Dpi.h>
#include <PolygonRasterizer.h>
#include <SavGolFilter.h>
#include <Transform.h>
#include <XSpline.h>
#include <imageproc/PolygonUtils.h>

#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <type_traits>

using namespace imageproc;

namespace output::detail {
uint8_t RaiseAboveBackground::transform(const uint8_t src, const uint8_t dst) {
  // src: orig
  // dst: background (dst >= src)
  if (dst - src < 1) {
    return 0xff;
  }
  const unsigned orig = src;
  const unsigned background = dst;
  return static_cast<uint8_t>((orig * 255 + background / 2) / background);
}

namespace {
template <typename PixelType>
PixelType reserveBlackAndWhite(PixelType color);

template <>
uint32_t reserveBlackAndWhite(uint32_t color) {
  // We handle both RGB32 and ARGB32 here.
  switch (color & 0x00FFFFFF) {
    case 0x00000000:
      return 0xFF010101;
    case 0x00FFFFFF:
      return 0xFFFEFEFE;
    default:
      return color;
  }
}

template <>
uint8_t reserveBlackAndWhite(uint8_t color) {
  switch (color) {
    case 0x00:
      return 0x01;
    case 0xFF:
      return 0xFE;
    default:
      return color;
  }
}

template <typename PixelType>
void reserveBlackAndWhite(QImage& img, const BinaryImage& mask) {
  const int width = img.width();
  const int height = img.height();

  auto* imageLine = reinterpret_cast<PixelType*>(img.bits());
  const int imageStride = img.bytesPerLine() / static_cast<int>(sizeof(PixelType));
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        imageLine[x] = reserveBlackAndWhite<PixelType>(imageLine[x]);
      }
    }
    imageLine += imageStride;
    maskLine += maskStride;
  }
}

template <typename MixedPixel>
void fillExcept(QImage& image, const BinaryImage& bwMask, const QColor& color) {
  auto* imageLine = reinterpret_cast<MixedPixel*>(image.bits());
  const int imageStride = image.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;
  MixedPixel fillingPixel;
  if constexpr (std::is_same_v<MixedPixel, uint8_t>) {
    // A gray pixel holds the gray of the color; the lowest byte of rgba() would be its blue part.
    fillingPixel = static_cast<uint8_t>(qGray(color.rgb()));
  } else {
    fillingPixel = static_cast<MixedPixel>(color.rgba());
  }

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        imageLine[x] = fillingPixel;
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}

void fillExcept(BinaryImage& image, const BinaryImage& bwMask, const BWColor color) {
  uint32_t* imageLine = image.data();
  const int imageStride = image.wordsPerLine();
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        if (color == BLACK) {
          imageLine[x >> 5] |= (msb >> (x & 31));
        } else {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        }
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}

// Clip content polygon to image rect to avoid exception when dewarp pushes content outside (issue #30).
QPolygonF clipContentPolyToRect(const QPolygonF& contentPoly, const QRect& imageRect) {
  const QRectF r(imageRect);
  QPolygonF rectPoly;
  rectPoly << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
  return PolygonUtils::round(contentPoly).intersected(rectPoly);
}

void ensureArgb32OrRgb32(QImage& image) {
  if ((image.format() != QImage::Format_RGB32) && (image.format() != QImage::Format_ARGB32)) {
    image = image.convertToFormat(image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
  }
}
}  // namespace

void reserveBlackAndWhite(QImage& img, const BinaryImage& mask) {
  switch (img.format()) {
    case QImage::Format_Indexed8:
      reserveBlackAndWhite<uint8_t>(img, mask);
      break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
      reserveBlackAndWhite<uint32_t>(img, mask);
      break;
    default:
      throw std::invalid_argument("reserveBlackAndWhite: wrong image format.");
  }
}

void fillMarginsInPlace(QImage& image, const QPolygonF& contentPoly, const QColor& color, const bool antialiasing) {
  const QRect imageRect = image.rect();
  QPolygonF clipped = clipContentPolyToRect(contentPoly, imageRect);
  if (clipped.isEmpty() || clipped.boundingRect().isEmpty()) {
    image.fill(color);
    return;
  }

  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    BinaryImage binaryImage(image);
    PolygonRasterizer::fillExcept(binaryImage, (color == Qt::black) ? BLACK : WHITE, clipped, Qt::WindingFill);
    image = binaryImage.toQImage();
    return;
  }
  if ((image.format() == QImage::Format_Indexed8) && image.isGrayscale()) {
    PolygonRasterizer::grayFillExcept(image, static_cast<unsigned char>(qGray(color.rgb())), clipped, Qt::WindingFill);
    return;
  }

  ensureArgb32OrRgb32(image);

  const QImage::Format imageFormat = image.format();
  image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
  {
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setBrush(color);
    painter.setPen(Qt::NoPen);

    QPainterPath outerPath;
    outerPath.addRect(image.rect());
    QPainterPath innerPath;
    innerPath.addPolygon(PolygonUtils::round(clipped));

    painter.drawPath(outerPath.subtracted(innerPath));
  }
  image = image.convertToFormat(imageFormat);
}

void fillMarginsInPlace(BinaryImage& image, const QPolygonF& contentPoly, const BWColor color) {
  const QRect imageRect = image.rect();
  QPolygonF clipped = clipContentPolyToRect(contentPoly, imageRect);
  if (clipped.isEmpty() || clipped.boundingRect().isEmpty()) {
    image.fill(color == BLACK ? BLACK : WHITE);
    return;
  }

  PolygonRasterizer::fillExcept(image, color, clipped, Qt::WindingFill);
}

void fillMarginsInPlace(BinaryImage& image, const BinaryImage& contentMask, const BWColor color) {
  if (image.size() != contentMask.size()) {
    throw std::invalid_argument("fillMarginsInPlace: image and mask have different sizes.");
  }

  fillExcept(image, contentMask, color);
}

void fillMarginsInPlace(QImage& image, const BinaryImage& contentMask, const QColor& color) {
  if (image.size() != contentMask.size()) {
    throw std::invalid_argument("fillMarginsInPlace: image and mask have different sizes.");
  }

  if ((image.format() == QImage::Format_Mono) || (image.format() == QImage::Format_MonoLSB)) {
    BinaryImage binaryImage(image);
    fillExcept(binaryImage, contentMask, (color == Qt::black) ? BLACK : WHITE);
    image = binaryImage.toQImage();
    return;
  }

  if ((image.format() == QImage::Format_Indexed8) && image.isGrayscale()) {
    fillExcept<uint8_t>(image, contentMask, color);
  } else {
    ensureArgb32OrRgb32(image);
    fillExcept<uint32_t>(image, contentMask, color);
  }
}

void BinaryImageXOR(BinaryImage& image, const BinaryImage& bwMask, const BWColor color) {
  uint32_t* imageLine = image.data();
  const int imageStride = image.wordsPerLine();
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;

  if (color == BLACK) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if ((imageLine[x >> 5] & (msb >> (x & 31))) != (bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
          imageLine[x >> 5] |= (msb >> (x & 31));
        } else {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        }
      }
      imageLine += imageStride;
      bwMaskLine += bwMaskStride;
    }
  } else {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        if ((imageLine[x >> 5] & (msb >> (x & 31))) != (bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
          imageLine[x >> 5] &= ~(msb >> (x & 31));
        } else {
          imageLine[x >> 5] |= (msb >> (x & 31));
        }
      }
      imageLine += imageStride;
      bwMaskLine += bwMaskStride;
    }
  }
}

void applyAffineTransform(QImage& image, const QTransform& xform, const QColor& outsideColor) {
  if (xform.isIdentity()) {
    return;
  }
  image = transform(image, xform, image.rect(), OutsidePixels::assumeWeakColor(outsideColor));
}

void applyAffineTransform(BinaryImage& image, const QTransform& xform, const BWColor outsideColor) {
  if (xform.isIdentity()) {
    return;
  }
  const QColor color = (outsideColor == BLACK) ? Qt::black : Qt::white;
  QImage converted = image.toQImage();
  applyAffineTransform(converted, xform, color);
  image = BinaryImage(converted);
}

void movePointToTopMargin(BinaryImage& bwImage, XSpline& spline, const int idx) {
  QPointF pos = spline.controlPointPosition(idx);
  const int x = static_cast<int>(pos.x());
  if ((x < 0) || (x >= bwImage.width())) {
    return;
  }
  const int checkNum = 16;

  for (int j = 0; (j < pos.y()) && (j + checkNum <= bwImage.height()); j++) {
    if (bwImage.getPixel(x, j) == WHITE) {
      int count = 0;

      for (int jj = j; jj < (j + checkNum); jj++) {
        if (bwImage.getPixel(x, jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);
        spline.moveControlPoint(idx, pos);
        break;
      }
    }
  }
}

void movePointToBottomMargin(BinaryImage& bwImage, XSpline& spline, const int idx) {
  QPointF pos = spline.controlPointPosition(idx);
  const int x = static_cast<int>(pos.x());
  if ((x < 0) || (x >= bwImage.width())) {
    return;
  }
  const int checkNum = 16;

  for (int j = bwImage.height() - 1; (j > pos.y()) && (j - checkNum + 1 >= 0); j--) {
    if (bwImage.getPixel(x, j) == WHITE) {
      int count = 0;

      for (int jj = j; jj > (j - checkNum); jj--) {
        if (bwImage.getPixel(x, jj) == WHITE) {
          count++;
        }
      }

      if (count == checkNum) {
        pos.setY(j);

        spline.moveControlPoint(idx, pos);

        break;
      }
    }
  }
}

void drawPoint(QImage& image, const QPointF& pt) {
  QPoint pts = pt.toPoint();

  for (int i = pts.x() - 10; i < pts.x() + 10; i++) {
    for (int j = pts.y() - 10; j < pts.y() + 10; j++) {
      QPoint p1(i, j);

      if (image.rect().contains(p1)) {
        image.setPixel(p1, qRgb(255, 0, 0));
      }
    }
  }
}

QImage smoothToGrayscale(const QImage& src, const Dpi& dpi) {
  const int minDpi = std::min(dpi.horizontal(), dpi.vertical());
  int window;
  int degree;
  if (minDpi <= 200) {
    window = 5;
    degree = 3;
  } else if (minDpi <= 400) {
    window = 7;
    degree = 4;
  } else if (minDpi <= 800) {
    window = 11;
    degree = 4;
  } else {
    window = 11;
    degree = 2;
  }
  return savGolFilter(src, QSize(window, window), degree, degree);
}

QSize to300dpi(const QSize& size, const Dpi& sourceDpi) {
  const double hscale = 300.0 / sourceDpi.horizontal();
  const double vscale = 300.0 / sourceDpi.vertical();
  const int width = qRound(size.width() * hscale);
  const int height = qRound(size.height() * vscale);
  return QSize(std::max(1, width), std::max(1, height));
}
}  // namespace output::detail
