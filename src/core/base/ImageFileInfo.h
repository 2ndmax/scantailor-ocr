// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGEFILEINFO_H_
#define SCANTAILOR_CORE_IMAGEFILEINFO_H_

#include <QFileInfo>
#include <QStringList>
#include <optional>
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
   * The metadata of the images of the file, as for a new project: one entry per page the loader
   * reports. Returns an empty optional if the file could not be loaded; then \p loadErrors (if not
   * null) holds why, with the generic text the app shows when the loader gives none. \p partialReads
   * (if not null) holds why only some of the pages were read; the returned info carries the same
   * reasons and can be checked with partialReads().
   */
  static std::optional<ImageFileInfo> load(const QFileInfo& fileInfo,
                                           QStringList* loadErrors,
                                           QStringList* partialReads);

  /**
   * The page numbers of the images in the file, one per entry of imageInfo(): from 1 in a
   * multi-page file, 0 in a single-page one. Empty if not known, e.g. for files to import.
   */
  const std::vector<int>& pageNumbers() const { return m_pageNumbers; }

  bool isDpiOK() const;

  /**
   * Why only some of the images of the file could be read (see
   * ImageLoadErrorCapture::addPartialRead()). Empty if the whole file was read.
   */
  const QStringList& partialReads() const { return m_partialReads; }

  void setPartialReads(const QStringList& reasons) { m_partialReads = reasons; }

 private:
  QFileInfo m_fileInfo;
  std::vector<ImageMetadata> m_imageInfo;
  std::vector<int> m_pageNumbers;
  QStringList m_partialReads;
};


#endif  // ifndef SCANTAILOR_CORE_IMAGEFILEINFO_H_
