// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTZONEOPS_H_
#define SCANTAILOR_OUTPUT_OUTPUTZONEOPS_H_

#include <BinaryImage.h>

#include <QImage>
#include <QPolygonF>
#include <QTransform>
#include <functional>

#include "ZoneSet.h"

namespace output::detail {
void removeAutoPictureZones(ZoneSet& pictureZones);

Zone createPictureZoneFromPoly(const QPolygonF& polygon);

void applyFillZonesInPlace(QImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput,
                           bool antialiasing = true);

void applyFillZonesInPlace(QImage& img, const ZoneSet& zones, const QTransform& transform, bool antialiasing = true);

void applyFillZonesInPlace(imageproc::BinaryImage& img,
                           const ZoneSet& zones,
                           const std::function<QPointF(const QPointF&)>& origToOutput);

void applyFillZonesInPlace(imageproc::BinaryImage& img, const ZoneSet& zones, const QTransform& transform);

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const std::function<QPointF(const QPointF&)>& origToOutput,
                                  const imageproc::BinaryImage& pictureMask,
                                  bool binaryMode);

void applyFillZonesToMixedInPlace(QImage& img,
                                  const ZoneSet& zones,
                                  const QTransform& transform,
                                  const imageproc::BinaryImage& pictureMask,
                                  bool binaryMode);

void applyFillZonesToMask(imageproc::BinaryImage& mask,
                          const ZoneSet& zones,
                          const std::function<QPointF(const QPointF&)>& origToOutput,
                          imageproc::BWColor fillColor = imageproc::BLACK);

void applyFillZonesToMask(imageproc::BinaryImage& mask,
                          const ZoneSet& zones,
                          const QTransform& transform,
                          imageproc::BWColor fillColor = imageproc::BLACK);
}  // namespace output::detail

#endif  // SCANTAILOR_OUTPUT_OUTPUTZONEOPS_H_
