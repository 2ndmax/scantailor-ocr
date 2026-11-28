// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_TASK_H_
#define SCANTAILOR_OUTPUT_TASK_H_

#include <QColor>
#include <QImage>
#include <memory>

#include "FilterResult.h"
#include "ImageViewTab.h"
#include "NonCopyable.h"
#include "OutputFileNameGenerator.h"
#include "PageId.h"
#include "ProcessingOptions.h"

class DebugImages;
class TaskStatus;
class FilterData;
class ThumbnailPixmapCache;
class ImageTransformation;
class QPolygonF;
class QRect;
class QSize;
class QImage;
class Dpi;
class ZoneSet;

namespace dewarping {
class DistortionModel;
}

namespace imageproc {
class BinaryImage;
}

namespace output {
class Filter;
class OutputGenerator;
class OutputImage;
class OutputImageParams;
class Params;
class RenderParams;
class Settings;

class Task {
  DECLARE_NON_COPYABLE(Task)

 public:
  Task(std::shared_ptr<Filter> filter,
       std::shared_ptr<Settings> settings,
       std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
       const PageId& pageId,
       const OutputFileNameGenerator& outFileNameGen,
       ImageViewTab lastTab,
       const ProcessingOptions& processingOptions,
       bool batch,
       bool debug);

  virtual ~Task();

  FilterResultPtr process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys);

 private:
  struct OutputPaths;

  OutputPaths resolveOutputPaths() const;

  void resetAutoPictureZonesIfShapeChanged(const Params& params) const;

  bool isCacheUpToDate(const OutputPaths& paths,
                       const OutputImageParams& newOutputImageParams,
                       const ZoneSet& newPictureZones,
                       const ZoneSet& newFillZones,
                       const RenderParams& renderParams,
                       bool needPictureEditor,
                       bool needSpecklesImage) const;

  bool loadCachedOutputs(const OutputPaths& paths,
                         const RenderParams& renderParams,
                         bool needPictureEditor,
                         bool needSpecklesImage,
                         QImage& outImg,
                         imageproc::BinaryImage& automaskImg,
                         imageproc::BinaryImage& specklesImg) const;

  void updateDistortionAndOutputParams(const dewarping::DistortionModel& distortionModel,
                                       Params& params,
                                       OutputImageParams& newOutputImageParams) const;

  std::unique_ptr<OutputImage> generateOutputImage(const TaskStatus& status,
                                                   const FilterData& data,
                                                   const OutputGenerator& generator,
                                                   ZoneSet& newPictureZones,
                                                   const ZoneSet& newFillZones,
                                                   bool writeAutomask,
                                                   bool writeSpecklesFile,
                                                   Params& params,
                                                   OutputImageParams& newOutputImageParams,
                                                   imageproc::BinaryImage& automaskImg,
                                                   imageproc::BinaryImage& specklesImg) const;

  bool writeSplitOutputs(const OutputPaths& paths,
                         const RenderParams& renderParams,
                         const OutputImage* outputImage) const;

  bool writeMainOutputImage(const QString& outFilePath, QImage& outImg);

  void persistOutputParams(const OutputPaths& paths,
                           const RenderParams& renderParams,
                           const OutputImageParams& newOutputImageParams,
                           const ZoneSet& newPictureZones,
                           const ZoneSet& newFillZones,
                           bool writeAutomask,
                           bool writeSpecklesFile,
                           bool invalidateParams) const;

  void writeOutputs(const OutputPaths& paths,
                    const RenderParams& renderParams,
                    const OutputImageParams& newOutputImageParams,
                    const ZoneSet& newPictureZones,
                    const ZoneSet& newFillZones,
                    bool writeAutomask,
                    bool writeSpecklesFile,
                    std::unique_ptr<OutputImage> outputImage,
                    QImage& outImg,
                    imageproc::BinaryImage& automaskImg,
                    imageproc::BinaryImage& specklesImg);

  FilterResultPtr buildResult(const Params& params,
                              const ImageTransformation& newXform,
                              const QRect& outputContentRect,
                              const QImage& origImage,
                              const QImage& outImg,
                              const imageproc::BinaryImage& automaskImg,
                              const imageproc::BinaryImage& specklesImg);

  void deleteMutuallyExclusiveOutputFiles();

  std::shared_ptr<Filter> m_filter;
  std::shared_ptr<Settings> m_settings;
  std::shared_ptr<ThumbnailPixmapCache> m_thumbnailCache;
  std::unique_ptr<DebugImages> m_dbg;
  PageId m_pageId;
  OutputFileNameGenerator m_outFileNameGen;
  ImageViewTab m_lastTab;
  ProcessingOptions m_processingOptions;
  bool m_batchProcessing;
  bool m_debug;
};
}  // namespace output
#endif  // ifndef SCANTAILOR_OUTPUT_TASK_H_
