// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_THUMBNAILCOLLECTOR_H_
#define SCANTAILOR_CORE_THUMBNAILCOLLECTOR_H_

#include <memory>

#include "AbstractFilterDataCollector.h"

class ThumbnailDataBase;

class ThumbnailCollector : public AbstractFilterDataCollector {
 public:
  /** What the thumbnail of the page shows; the user interface draws it. */
  virtual void processThumbnail(std::unique_ptr<ThumbnailDataBase> thumbnail) = 0;
};


#endif
