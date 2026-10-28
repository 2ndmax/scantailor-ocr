// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_SPOTSHEET_H_
#define SCANTAILOR_TOOLS_SPOTSHEET_H_

#include <Despeckle.h>

#include <QPoint>
#include <QString>
#include <vector>

class QImage;

struct LevelName {
  Despeckle::Level level;
  const char* name;
};

extern const LevelName kLevels[3];

// Writes enlarged crops of 'image' around the given points, one per 60x60 cell, at most 48.
// Writes nothing if there are no points.
void saveSpotSheet(const QImage& image, const std::vector<QPoint>& points, const QString& path);

#endif  // SCANTAILOR_TOOLS_SPOTSHEET_H_
