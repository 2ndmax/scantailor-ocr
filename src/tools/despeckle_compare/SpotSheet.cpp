// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SpotSheet.h"

#include <QImage>
#include <QPainter>
#include <algorithm>

const LevelName kLevels[3]
    = {{Despeckle::CAUTIOUS, "cautious"}, {Despeckle::NORMAL, "normal"}, {Despeckle::AGGRESSIVE, "aggressive"}};

void saveSpotSheet(const QImage& image, const std::vector<QPoint>& points, const QString& path) {
  const int cell = 60;
  const int scale = 4;
  std::vector<QPoint> spots;
  for (const QPoint& p : points) {
    const QPoint key(p.x() / cell, p.y() / cell);
    if (std::find(spots.begin(), spots.end(), key) == spots.end()) {
      spots.push_back(key);
    }
  }
  if (spots.empty()) {
    return;
  }
  const int count = std::min<int>(int(spots.size()), 48);
  const int cols = 8;
  const int rows = (count + cols - 1) / cols;
  const int tile = cell * scale;
  QImage sheet(cols * (tile + 4), rows * (tile + 4), QImage::Format_RGB32);
  sheet.fill(qRgb(80, 80, 80));
  QPainter painter(&sheet);
  for (int i = 0; i < count; ++i) {
    const QRect src(spots[i].x() * cell - cell / 2, spots[i].y() * cell - cell / 2, cell * 2, cell * 2);
    const QImage crop = image.copy(src).scaled(tile, tile, Qt::KeepAspectRatio, Qt::FastTransformation);
    painter.drawImage((i % cols) * (tile + 4), (i / cols) * (tile + 4), crop);
  }
  painter.end();
  sheet.save(path);
}
