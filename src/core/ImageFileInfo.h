// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGEFILEINFO_H_
#define SCANTAILOR_CORE_IMAGEFILEINFO_H_

#include <QFileInfo>
#include <vector>

#include "ImageMetadata.h"

class ImageFileInfo {
  // Member-wise copying is OK.
 public:
  ImageFileInfo(const QFileInfo& fileInfo, const std::vector<ImageMetadata>& imageInfo)
      : m_fileInfo(fileInfo), m_imageInfo(imageInfo) {}

  /** \p pageNumbers: see pageNumbers(). */
  ImageFileInfo(const QFileInfo& fileInfo,
                const std::vector<ImageMetadata>& imageInfo,
                const std::vector<int>& pageNumbers)
      : m_fileInfo(fileInfo), m_imageInfo(imageInfo), m_pageNumbers(pageNumbers) {}

  const QFileInfo& fileInfo() const { return m_fileInfo; }

  std::vector<ImageMetadata>& imageInfo() { return m_imageInfo; }

  const std::vector<ImageMetadata>& imageInfo() const { return m_imageInfo; }

  /**
   * The page numbers of the images in the file, one per entry of imageInfo(): from 1 in a
   * multi-page file, 0 in a single-page one. Empty if not known, e.g. for files to import.
   */
  const std::vector<int>& pageNumbers() const { return m_pageNumbers; }

  bool isDpiOK() const;

 private:
  QFileInfo m_fileInfo;
  std::vector<ImageMetadata> m_imageInfo;
  std::vector<int> m_pageNumbers;
};


#endif  // ifndef SCANTAILOR_CORE_IMAGEFILEINFO_H_
