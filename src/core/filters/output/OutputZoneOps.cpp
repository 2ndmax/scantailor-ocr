// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputZoneOps.h"

#include <Grayscale.h>
#include <PolygonRasterizer.h>
#include <imageproc/ImageCombination.h>

#include <QPainter>
#include <functional>

#include <SerializableSpline.h>

#include "FillColorProperty.h"
#include "PictureLayerProperty.h"
#include "ZoneCategoryProperty.h"

using namespace imageproc;

namespace output::detail {
namespace {
using MapPointFunc = QPointF (QTransform::*)(const QPointF&) const;
}  // namespace

void removeAutoPictureZones(ZoneSet& pictureZones) {
  for (auto it = pictureZones.begin(); it != pictureZones.end();) {
    const Zone& zone = *it;
    if (zone.properties().locateOrDefault<ZoneCategoryProperty>()->zoneCategory() == ZoneCategoryProperty::AUTO) {
      it = pictureZones.erase(it);
    } else {
      ++it;
    }
  }
}

Zone createPictureZoneFromPoly(const QPolygonF& polygon) {
  PropertySet propertySet;
  propertySet.locateOrCreate<output::PictureLayerProperty>()->setLayer(output::PictureLayerProperty::ZONEPAINTER2);
  propertySet.locateOrCreate<output::ZoneCategoryProperty>()->setZoneCategory(ZoneCategoryProperty::AUTO);
  return Zone(SerializableSpline(polygon), propertySet);
}

void applyFillZonesInPlace(QImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput,
                           const bool antialiasing) {
  if (zones.empty()) {
    return;
  }

  QImage canvas(img.convertToFormat(QImage::Format_ARGB32_Premultiplied));
  {
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing, antialiasing);
    painter.setPen(Qt::NoPen);

    for (const Zone& zone : zones) {
      const QColor color(zone.properties().locateOrDefault<FillColorProperty>()->color());
      const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
      painter.setBrush(color);
      painter.drawPolygon(poly, Qt::WindingFill);
    }
  }

  if ((img.format() == QImage::Format_Indexed8) && img.isGrayscale()) {
    img = toGrayscale(canvas);
  } else {
    img = canvas.convertToFormat(img.format());
  }
}

void applyFillZonesInPlace(QImage& img, const ZoneSet& zones, const QTransform& transform, const bool antialiasing) {
  applyFillZonesInPlace(img, zones,
                        std::bind(static_cast<MapPointFunc>(&QTransform::map), transform, std::placeholders::_1),
                        antialiasing);
}

void applyFillZonesInPlace(BinaryImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput) {
  if (zones.empty()) {
    return;
  }

  for (const Zone& zone : zones) {
    const QColor color(zone.properties().locateOrDefault<FillColorProperty>()->color());
    const BWColor bwColor = qGray(color.rgb()) < 128 ? BLACK : WHITE;
    const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
    PolygonRasterizer::fill(img, bwColor, poly, Qt::WindingFill);
  }
}

void applyFillZonesInPlace(BinaryImage& img, const ZoneSet& zones, const QTransform& transform) {
  applyFillZonesInPlace(img, zones,
                        std::bind(static_cast<MapPointFunc>(&QTransform::map), transform, std::placeholders::_1));
}

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const std::function<QPointF(const QPointF&)>& origToOutput,
                                  const BinaryImage& pictureMask,
                                  const bool binaryMode) {
  if (binaryMode) {
    BinaryImage bwContent(img, BinaryThreshold(1));
    applyFillZonesInPlace(bwContent, zones, origToOutput);
    applyFillZonesInPlace(img, zones, origToOutput);
    combineImages(img, bwContent, pictureMask);
  } else {
    QImage content(img);
    applyMask(content, pictureMask);
    applyFillZonesInPlace(content, zones, origToOutput, false);
    applyFillZonesInPlace(img, zones, origToOutput);
    combineImages(img, content, pictureMask);
  }
}

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const QTransform& transform,
                                  const BinaryImage& pictureMask,
                                  const bool binaryMode) {
  applyFillZonesToMixedInPlace(img, zones,
                               std::bind(static_cast<MapPointFunc>(&QTransform::map), transform, std::placeholders::_1),
                               pictureMask, binaryMode);
}

void applyFillZonesToMask(BinaryImage& mask,
                          const ZoneSet& zones,
                          const std::function<QPointF(const QPointF&)>& origToOutput,
                          const BWColor fillColor) {
  if (zones.empty()) {
    return;
  }

  for (const Zone& zone : zones) {
    const QPolygonF poly(zone.spline().transformed(origToOutput).toPolygon());
    PolygonRasterizer::fill(mask, fillColor, poly, Qt::WindingFill);
  }
}

void applyFillZonesToMask(BinaryImage& mask,
                          const ZoneSet& zones,
                          const QTransform& transform,
                          const BWColor fillColor) {
  applyFillZonesToMask(mask, zones, std::bind((MapPointFunc) &QTransform::map, transform, std::placeholders::_1),
                       fillColor);
}
}  // namespace output::detail
