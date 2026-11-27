// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Transform.h"

#include <QDebug>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>

#include "BadAllocIfNull.h"
#include "ColorMixer.h"
#include "Grayscale.h"

namespace imageproc {
namespace {
struct XLess {
  bool operator()(const QPointF& lhs, const QPointF& rhs) const { return lhs.x() < rhs.x(); }
};

struct YLess {
  bool operator()(const QPointF& lhs, const QPointF& rhs) const { return lhs.y() < rhs.y(); }
};

QSizeF calcSrcUnitSize(const QTransform& xform, const QSizeF& min) {
  // Imagine a rectangle of (0, 0, 1, 1), except we take
  // centers of its edges instead of its vertices.
  QPolygonF dstPoly;
  dstPoly.push_back(QPointF(0.5, 0.0));
  dstPoly.push_back(QPointF(1.0, 0.5));
  dstPoly.push_back(QPointF(0.5, 1.0));
  dstPoly.push_back(QPointF(0.0, 0.5));

  QPolygonF srcPoly(xform.map(dstPoly));
  std::sort(srcPoly.begin(), srcPoly.end(), XLess());
  const double width = srcPoly.back().x() - srcPoly.front().x();
  std::sort(srcPoly.begin(), srcPoly.end(), YLess());
  const double height = srcPoly.back().y() - srcPoly.front().y();

  const QSizeF min32(min * 32.0);
  return QSizeF(std::max(min32.width(), width), std::max(min32.height(), height));
}

template <typename StorageUnit, typename Mixer>
static void transformGeneric(const StorageUnit* const srcData,
                             const int srcStride,
                             const QSize srcSize,
                             StorageUnit* const dstData,
                             const int dstStride,
                             const QTransform& xform,
                             const QRect& dstRect,
                             const StorageUnit outsideColor,
                             const int outsideFlags,
                             const QSizeF& minMappingArea) {
  const int sw = srcSize.width();
  const int sh = srcSize.height();
  const int dw = dstRect.width();
  const int dh = dstRect.height();

  StorageUnit* dstLine = dstData;

  QTransform invXform;
  invXform.translate(dstRect.x(), dstRect.y());
  bool invertible = false;
  const QTransform inv = xform.inverted(&invertible);
  if (!invertible) {
    throw std::invalid_argument("transform: singular transformation matrix");
  }
  invXform *= inv;
  invXform *= QTransform().scale(32.0, 32.0);

  // sx32 = dx*invXform.m11() + dy*invXform.m21() + invXform.dx();
  // sy32 = dy*invXform.m22() + dx*invXform.m12() + invXform.dy();

  const QSizeF src32UnitSize(calcSrcUnitSize(invXform, minMappingArea));
  if (!std::isfinite(src32UnitSize.width()) || !std::isfinite(src32UnitSize.height())) {
    throw std::invalid_argument("transform: non-finite transformation matrix");
  }
  const double unitW = std::max(1.0, std::round(src32UnitSize.width()));
  const double unitH = std::max(1.0, std::round(src32UnitSize.height()));
  constexpr double kMaxSubpixelCoord = 1e7;
  const int src32UnitW = static_cast<int>(std::clamp(unitW, 1.0, kMaxSubpixelCoord));
  const int src32UnitH = static_cast<int>(std::clamp(unitH, 1.0, kMaxSubpixelCoord));

  for (int dy = 0; dy < dh; ++dy, dstLine += dstStride) {
    const double fDyCenter = dy + 0.5;
    const double fSx32Base = fDyCenter * invXform.m21() + invXform.dx();
    const double fSy32Base = fDyCenter * invXform.m22() + invXform.dy();

    for (int dx = 0; dx < dw; ++dx) {
      const double fDxCenter = dx + 0.5;
      const double fSx32Center = fSx32Base + fDxCenter * invXform.m11();
      const double fSy32Center = fSy32Base + fDxCenter * invXform.m12();
      const double fSrc32Left = fSx32Center - 0.5 * unitW;
      const double fSrc32Top = fSy32Center - 0.5 * unitH;
      const double fSrc32Right = fSrc32Left + unitW;
      const double fSrc32Bottom = fSrc32Top + unitH;

      if (!std::isfinite(fSx32Center) || !std::isfinite(fSy32Center) || (fSrc32Right <= 0.0)
          || (fSrc32Left >= static_cast<double>(sw) * 32.0) || (fSrc32Bottom <= 0.0)
          || (fSrc32Top >= static_cast<double>(sh) * 32.0)) {
        // Completely outside of src image.
        if ((outsideFlags & OutsidePixels::COLOR) || !std::isfinite(fSx32Center) || !std::isfinite(fSy32Center)) {
          dstLine[dx] = outsideColor;
        } else {
          const double fSrcX = std::floor(fSx32Center / 32.0);
          const double fSrcY = std::floor(fSy32Center / 32.0);
          const int srcX = (fSrcX <= 0.0) ? 0 : ((fSrcX >= sw - 1) ? (sw - 1) : static_cast<int>(fSrcX));
          const int srcY = (fSrcY <= 0.0) ? 0 : ((fSrcY >= sh - 1) ? (sh - 1) : static_cast<int>(fSrcY));
          dstLine[dx] = srcData[srcY * srcStride + srcX];
        }
        continue;
      }

      const double clampedCenterX = std::clamp(fSx32Center, -kMaxSubpixelCoord, kMaxSubpixelCoord);
      const double clampedCenterY = std::clamp(fSy32Center, -kMaxSubpixelCoord, kMaxSubpixelCoord);
      int src32Left = static_cast<int>(clampedCenterX) - (src32UnitW >> 1);
      int src32Top = static_cast<int>(clampedCenterY) - (src32UnitH >> 1);
      int src32Right = src32Left + src32UnitW;
      int src32Bottom = src32Top + src32UnitH;
      int srcLeft = src32Left >> 5;
      int srcRight = (src32Right - 1) >> 5;  // inclusive
      int srcTop = src32Top >> 5;
      int srcBottom = (src32Bottom - 1) >> 5;  // inclusive
      assert(srcBottom >= srcTop);
      assert(srcRight >= srcLeft);

      if ((srcBottom < 0) || (srcRight < 0) || (srcLeft >= sw) || (srcTop >= sh)) {
        // Completely outside of src image.
        if (outsideFlags & OutsidePixels::COLOR) {
          dstLine[dx] = outsideColor;
        } else {
          const int srcX = qBound<int>(0, (srcLeft + srcRight) >> 1, sw - 1);
          const int srcY = qBound<int>(0, (srcTop + srcBottom) >> 1, sh - 1);
          dstLine[dx] = srcData[srcY * srcStride + srcX];
        }
        continue;
      }

      /*
       * Note that (intval / 32) is not the same as (intval >> 5).
       * The former rounds towards zero, while the latter rounds towards
       * negative infinity.
       * Likewise, (intval % 32) is not the same as (intval & 31).
       * The following expression:
       * topFraction = 32 - (src32Top & 31);
       * works correctly with both positive and negative src32Top.
       */

      using MixingWeight = uint64_t;
      using AccumType = typename Mixer::accumType;
      MixingWeight backgroundArea = 0;

      if (srcTop < 0) {
        const MixingWeight topFraction = 32 - (src32Top & 31);
        const MixingWeight horFraction = src32Right - src32Left;
        backgroundArea += topFraction * horFraction;
        const MixingWeight fullPixelsVer = -1 - srcTop;
        backgroundArea += horFraction * (fullPixelsVer << 5);
        srcTop = 0;
        src32Top = 0;
      }
      if (srcBottom >= sh) {
        const MixingWeight bottomFraction = src32Bottom - (srcBottom << 5);
        const MixingWeight horFraction = src32Right - src32Left;
        backgroundArea += bottomFraction * horFraction;
        const MixingWeight fullPixelsVer = srcBottom - sh;
        backgroundArea += horFraction * (fullPixelsVer << 5);
        srcBottom = sh - 1;     // inclusive
        src32Bottom = sh << 5;  // exclusive
      }
      if (srcLeft < 0) {
        const MixingWeight leftFraction = 32 - (src32Left & 31);
        const MixingWeight vertFraction = src32Bottom - src32Top;
        backgroundArea += leftFraction * vertFraction;
        const MixingWeight fullPixelsHor = -1 - srcLeft;
        backgroundArea += vertFraction * (fullPixelsHor << 5);
        srcLeft = 0;
        src32Left = 0;
      }
      if (srcRight >= sw) {
        const MixingWeight rightFraction = src32Right - (srcRight << 5);
        const MixingWeight vertFraction = src32Bottom - src32Top;
        backgroundArea += rightFraction * vertFraction;
        const MixingWeight fullPixelsHor = srcRight - sw;
        backgroundArea += vertFraction * (fullPixelsHor << 5);
        srcRight = sw - 1;     // inclusive
        src32Right = sw << 5;  // exclusive
      }
      assert(srcBottom >= srcTop);
      assert(srcRight >= srcLeft);

      Mixer mixer;
      if (outsideFlags & OutsidePixels::WEAK) {
        backgroundArea = 0;
      } else {
        assert(outsideFlags & OutsidePixels::COLOR);
        mixer.add(outsideColor, static_cast<AccumType>(backgroundArea));
      }

      const MixingWeight leftFraction = 32 - (src32Left & 31);
      const MixingWeight topFraction = 32 - (src32Top & 31);
      const MixingWeight rightFraction = src32Right - (srcRight << 5);
      const MixingWeight bottomFraction = src32Bottom - (srcBottom << 5);

      assert(leftFraction + rightFraction + static_cast<MixingWeight>(srcRight - srcLeft - 1) * 32
             == static_cast<MixingWeight>(src32Right - src32Left));
      assert(topFraction + bottomFraction + static_cast<MixingWeight>(srcBottom - srcTop - 1) * 32
             == static_cast<MixingWeight>(src32Bottom - src32Top));

      const MixingWeight srcArea = static_cast<MixingWeight>(src32Bottom - src32Top) * (src32Right - src32Left);
      if (srcArea == 0) {
        if ((outsideFlags & OutsidePixels::COLOR)) {
          dstLine[dx] = outsideColor;
        } else {
          const int srcX = qBound<int>(0, (srcLeft + srcRight) >> 1, sw - 1);
          const int srcY = qBound<int>(0, (srcTop + srcBottom) >> 1, sh - 1);
          dstLine[dx] = srcData[srcY * srcStride + srcX];
        }
        continue;
      }

      const StorageUnit* srcLine = &srcData[srcTop * srcStride];

      if (srcTop == srcBottom) {
        if (srcLeft == srcRight) {
          // dst pixel maps to a single src pixel
          const StorageUnit c = srcLine[srcLeft];
          if (backgroundArea == 0) {
            // common case optimization
            dstLine[dx] = c;
            continue;
          }
          mixer.add(c, static_cast<AccumType>(srcArea));
        } else {
          // dst pixel maps to a horizontal line of src pixels
          const MixingWeight vertFraction = src32Bottom - src32Top;
          const MixingWeight leftArea = vertFraction * leftFraction;
          const MixingWeight middleArea = vertFraction << 5;
          const MixingWeight rightArea = vertFraction * rightFraction;

          mixer.add(srcLine[srcLeft], static_cast<AccumType>(leftArea));

          for (int sx = srcLeft + 1; sx < srcRight; ++sx) {
            mixer.add(srcLine[sx], static_cast<AccumType>(middleArea));
          }

          mixer.add(srcLine[srcRight], static_cast<AccumType>(rightArea));
        }
      } else if (srcLeft == srcRight) {
        // dst pixel maps to a vertical line of src pixels
        const MixingWeight horFraction = src32Right - src32Left;
        const MixingWeight topArea = horFraction * topFraction;
        const MixingWeight middleArea = horFraction << 5;
        const MixingWeight bottomArea = horFraction * bottomFraction;

        srcLine += srcLeft;
        mixer.add(*srcLine, static_cast<AccumType>(topArea));

        srcLine += srcStride;

        for (int sy = srcTop + 1; sy < srcBottom; ++sy) {
          mixer.add(*srcLine, static_cast<AccumType>(middleArea));
          srcLine += srcStride;
        }

        mixer.add(*srcLine, static_cast<AccumType>(bottomArea));
      } else {
        // dst pixel maps to a block of src pixels
        const MixingWeight topArea = topFraction << 5;
        const MixingWeight bottomArea = bottomFraction << 5;
        const MixingWeight leftArea = leftFraction << 5;
        const MixingWeight rightArea = rightFraction << 5;
        const MixingWeight topleftArea = topFraction * leftFraction;
        const MixingWeight toprightArea = topFraction * rightFraction;
        const MixingWeight bottomleftArea = bottomFraction * leftFraction;
        const MixingWeight bottomrightArea = bottomFraction * rightFraction;

        // process the top-left corner
        mixer.add(srcLine[srcLeft], static_cast<AccumType>(topleftArea));

        // process the top line (without corners)
        for (int sx = srcLeft + 1; sx < srcRight; ++sx) {
          mixer.add(srcLine[sx], static_cast<AccumType>(topArea));
        }

        // process the top-right corner
        mixer.add(srcLine[srcRight], static_cast<AccumType>(toprightArea));

        srcLine += srcStride;
        // process middle lines
        for (int sy = srcTop + 1; sy < srcBottom; ++sy) {
          mixer.add(srcLine[srcLeft], static_cast<AccumType>(leftArea));

          for (int sx = srcLeft + 1; sx < srcRight; ++sx) {
            mixer.add(srcLine[sx], static_cast<AccumType>(MixingWeight(32 * 32)));
          }

          mixer.add(srcLine[srcRight], static_cast<AccumType>(rightArea));

          srcLine += srcStride;
        }

        // process bottom-left corner
        mixer.add(srcLine[srcLeft], static_cast<AccumType>(bottomleftArea));

        // process the bottom line (without corners)
        for (int sx = srcLeft + 1; sx < srcRight; ++sx) {
          mixer.add(srcLine[sx], static_cast<AccumType>(bottomArea));
        }

        // process the bottom-right corner
        mixer.add(srcLine[srcRight], static_cast<AccumType>(bottomrightArea));
      }

      dstLine[dx] = mixer.mix(static_cast<AccumType>(srcArea + backgroundArea));
    }
  }
}  // transformGeneric

template <typename ImageT>
void fixDpiInPlace(ImageT& dst, const QImage& src, const QTransform& xform) {
  QLineF horLine(0, 0, src.dotsPerMeterX(), 0);
  QLineF verLine(0, 0, 0, src.dotsPerMeterY());
  dst.setDotsPerMeterX(qRound(xform.map(horLine).length()));
  dst.setDotsPerMeterY(qRound(xform.map(verLine).length()));
}
}  // namespace

QImage transform(const QImage& src,
                 const QTransform& xform,
                 const QRect& dstRect,
                 const OutsidePixels outsidePixels,
                 const QSizeF& minMappingArea) {
  if (src.isNull() || dstRect.isEmpty()) {
    return QImage();
  }
  if (!xform.isAffine()) {
    throw std::invalid_argument("transform: only affine transformations are supported");
  }
  if (!dstRect.isValid()) {
    throw std::invalid_argument("transform: dstRect is invalid");
  }

  auto isOpaqueGray
      = [](QRgb rgba) { return qAlpha(rgba) == 0xff && qRed(rgba) == qBlue(rgba) && qRed(rgba) == qGreen(rgba); };
  switch (src.format()) {
    case QImage::Format_Invalid:
      return QImage();
    case QImage::Format_Indexed8:
    case QImage::Format_Mono:
    case QImage::Format_MonoLSB:
      // allGray() ignores the alpha of the palette, so a transparent gray palette takes the 32-bit path.
      if (src.allGray() && !src.hasAlphaChannel() && isOpaqueGray(outsidePixels.rgba())) {
        // The palette of src may be non-standard, so we create a GrayImage,
        // which is guaranteed to have a standard palette.
        GrayImage graySrc(src);
        GrayImage grayDst(dstRect.size());
        using AccumType = uint64_t;
        transformGeneric<uint8_t, GrayColorMixer<AccumType>>(
            graySrc.data(), graySrc.stride(), src.size(), grayDst.data(), grayDst.stride(), xform, dstRect,
            outsidePixels.grayLevel(), outsidePixels.flags(), minMappingArea);

        fixDpiInPlace(grayDst, src, xform);
        return grayDst;
      }
      // fall through
    default:
      if (!src.hasAlphaChannel() && (qAlpha(outsidePixels.rgba()) == 0xff)) {
        const QImage srcRgb32(src.convertToFormat(QImage::Format_RGB32));
        badAllocIfNull(srcRgb32);
        QImage dst(dstRect.size(), QImage::Format_RGB32);
        badAllocIfNull(dst);

        using AccumType = uint64_t;
        transformGeneric<uint32_t, RgbColorMixer<AccumType>>(
            (const uint32_t*) srcRgb32.bits(), srcRgb32.bytesPerLine() / 4, srcRgb32.size(), (uint32_t*) dst.bits(),
            dst.bytesPerLine() / 4, xform, dstRect, outsidePixels.rgb(), outsidePixels.flags(), minMappingArea);

        fixDpiInPlace(dst, src, xform);
        return dst;
      } else {
        const QImage srcArgb32(src.convertToFormat(QImage::Format_ARGB32));
        badAllocIfNull(srcArgb32);
        QImage dst(dstRect.size(), QImage::Format_ARGB32);
        badAllocIfNull(dst);

        using AccumType = float;
        transformGeneric<uint32_t, ArgbColorMixer<AccumType>>(
            (const uint32_t*) srcArgb32.bits(), srcArgb32.bytesPerLine() / 4, srcArgb32.size(), (uint32_t*) dst.bits(),
            dst.bytesPerLine() / 4, xform, dstRect, outsidePixels.rgba(), outsidePixels.flags(), minMappingArea);

        fixDpiInPlace(dst, src, xform);
        return dst;
      }
  }
}  // transform

GrayImage transformToGray(const QImage& src,
                          const QTransform& xform,
                          const QRect& dstRect,
                          const OutsidePixels outsidePixels,
                          const QSizeF& minMappingArea) {
  if (src.isNull() || dstRect.isEmpty()) {
    return GrayImage();
  }
  if (!xform.isAffine()) {
    throw std::invalid_argument("transformToGray: only affine transformations are supported");
  }
  if (!dstRect.isValid()) {
    throw std::invalid_argument("transformToGray: dstRect is invalid");
  }

  const GrayImage graySrc(src);
  GrayImage dst(dstRect.size());

  using AccumType = uint64_t;
  transformGeneric<uint8_t, GrayColorMixer<AccumType>>(graySrc.data(), graySrc.stride(), graySrc.size(), dst.data(),
                                                       dst.stride(), xform, dstRect, outsidePixels.grayLevel(),
                                                       outsidePixels.flags(), minMappingArea);

  fixDpiInPlace(dst, src, xform);
  return dst;
}
}  // namespace imageproc
