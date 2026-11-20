// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_BATCHPROCESSINGRESULT_H_
#define SCANTAILOR_CORE_BATCHPROCESSINGRESULT_H_

#include <memory>
#include <utility>

#include "FilterResult.h"
#include "FilterUiInterface.h"
#include "PageId.h"

/**
 * What a step returns for a page in batch processing. Batch processing shows no page, only
 * the thumbnails, so the step doesn't prepare the images of its view: that would cost time and
 * memory on every page for nothing.
 */
class BatchProcessingResult : public FilterResult {
 public:
  enum Thumbnails {
    /** The page's thumbnail is redrawn. */
    PAGE_THUMBNAIL,
    /** All thumbnails are redrawn, e.g. because the size of all pages changed. */
    ALL_THUMBNAILS
  };

  BatchProcessingResult(std::shared_ptr<AbstractFilter> filter,
                        const PageId& pageId,
                        const Thumbnails thumbnails = PAGE_THUMBNAIL)
      : m_filter(std::move(filter)), m_pageId(pageId), m_thumbnails(thumbnails) {}

  void updateUI(FilterUiInterface* ui) override {
    // This function is executed from the GUI thread.
    if (m_thumbnails == ALL_THUMBNAILS) {
      ui->invalidateAllThumbnails();
    } else {
      ui->invalidateThumbnail(m_pageId);
    }
  }

  std::shared_ptr<AbstractFilter> filter() override { return m_filter; }

 private:
  std::shared_ptr<AbstractFilter> m_filter;
  PageId m_pageId;
  Thumbnails m_thumbnails;
};

#endif  // SCANTAILOR_CORE_BATCHPROCESSINGRESULT_H_
