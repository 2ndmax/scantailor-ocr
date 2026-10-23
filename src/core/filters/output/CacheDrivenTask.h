// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_CACHEDRIVENTASK_H_
#define SCANTAILOR_OUTPUT_CACHEDRIVENTASK_H_

#include <memory>

#include "NonCopyable.h"
#include "OutputFileNameGenerator.h"

class QPolygonF;
class PageInfo;
class AbstractFilterDataCollector;
class ImageTransformation;

namespace output {
class Settings;

class CacheDrivenTask {
  DECLARE_NON_COPYABLE(CacheDrivenTask)

 public:
  CacheDrivenTask(std::shared_ptr<Settings> settings, const OutputFileNameGenerator& outFileNameGen);

  virtual ~CacheDrivenTask();

  void process(const PageInfo& pageInfo,
               AbstractFilterDataCollector* collector,
               const ImageTransformation& xform,
               const QPolygonF& contentRectPhys);

 private:
  /**
   * Whether the stored output of the page matches its current settings and its files are
   * unchanged. \p xform is already scaled to the output resolution.
   */
  bool isOutputUpToDate(const PageInfo& pageInfo,
                        const ImageTransformation& xform,
                        const QPolygonF& contentRectPhys) const;

  std::shared_ptr<Settings> m_settings;
  OutputFileNameGenerator m_outFileNameGen;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_CACHEDRIVENTASK_H_
