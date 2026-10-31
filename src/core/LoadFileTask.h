// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_LOADFILETASK_H_
#define SCANTAILOR_CORE_LOADFILETASK_H_

#include <QStringList>
#include <memory>

#include "BackgroundTask.h"
#include "FilterResult.h"
#include "ImageId.h"
#include "ImageMetadata.h"
#include "NonCopyable.h"

class ThumbnailPixmapCache;
class PageInfo;
class ProjectPages;
class QImage;

namespace fix_orientation {
class Task;
}

class LoadFileTask : public BackgroundTask {
  DECLARE_NON_COPYABLE(LoadFileTask)

 public:
  LoadFileTask(Type type,
               const PageInfo& page,
               std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
               std::shared_ptr<ProjectPages> pages,
               std::shared_ptr<fix_orientation::Task> nextTask);

  ~LoadFileTask() override;

  /** Why a page couldn't be shown or output. */
  enum class Failure {
    /** The image file couldn't be loaded. */
    LOAD,
    /** The file was loaded, but processing it failed with an unexpected error. */
    PROCESSING,
    /** The output page would exceed ImageSizeLimit with the current settings. */
    OUTPUT_TOO_LARGE
  };

  FilterResultPtr operator()() override;

  /**
   * Whether \p result is that of a page that couldn't be loaded or processed. If so, \p reasons
   * gets why, and \p failure what went wrong.
   */
  static bool isFailure(const FilterResultPtr& result, QStringList* reasons, Failure* failure);

 private:
  class ErrorResult;

  void updateImageSizeIfChanged(const QImage& image);

  void overrideDpi(QImage& image) const;

  void convertToSupportedFormat(QImage& image) const;

  /**
   * Reports a processing error (Failure::PROCESSING or Failure::OUTPUT_TOO_LARGE) and returns a
   * result showing it in place of the page.
   */
  FilterResultPtr processingFailed(const QString& reason, Failure failure = Failure::PROCESSING) const;

  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  ImageId m_imageId;
  ImageMetadata m_imageMetadata;
  const std::shared_ptr<ProjectPages> m_pages;
  const std::shared_ptr<fix_orientation::Task> m_nextTask;
};


#endif  // ifndef SCANTAILOR_CORE_LOADFILETASK_H_
