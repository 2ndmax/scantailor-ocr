// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageId.h"

#include <QFileInfo>

ImageId::ImageId(const QString& filePath, const int page) : m_filePath(filePath), m_page(page) {}

ImageId::ImageId(const QFileInfo& fileInfo, const int page) : m_filePath(fileInfo.absoluteFilePath()), m_page(page) {}

ImageId ImageId::ofImageInFile(const QString& filePath, const int numImagesInFile, const int zeroBasedIndex) {
  const int multiPageBase = (numImagesInFile > 1) ? 1 : 0;
  return ImageId(filePath, multiPageBase + zeroBasedIndex);
}

bool operator==(const ImageId& lhs, const ImageId& rhs) {
  return ((lhs.page() == rhs.page()) && (lhs.filePath() == rhs.filePath()));
}

bool operator!=(const ImageId& lhs, const ImageId& rhs) {
  return !(lhs == rhs);
}

bool operator<(const ImageId& lhs, const ImageId& rhs) {
  // REVIEW-NOTE (2026-10): paths are compared case-sensitively, also on Windows -> the same file added via
  // differently cased paths becomes two images. Not fixed because changing comparison and hashing affects
  // existing projects.
  const int comp = lhs.filePath().compare(rhs.filePath());
  if (comp < 0) {
    return true;
  } else if (comp > 0) {
    return false;
  }
  return lhs.page() < rhs.page();
}
