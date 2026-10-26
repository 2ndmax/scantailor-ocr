// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CacheDrivenTask.h"

#include <QFileInfo>
#include <utility>

#include "GeometryCollector.h"
#include "IncompleteThumbnail.h"
#include "OutputGenerator.h"
#include "OutputParams.h"
#include "OutputStatusCollector.h"
#include "PageInfo.h"
#include "Params.h"
#include "RenderParams.h"
#include "Settings.h"
#include "Thumbnail.h"
#include "core/AbstractFilterDataCollector.h"
#include "core/ThumbnailCollector.h"

namespace output {
CacheDrivenTask::CacheDrivenTask(std::shared_ptr<Settings> settings, const OutputFileNameGenerator& outFileNameGen)
    : m_settings(std::move(settings)), m_outFileNameGen(outFileNameGen) {}

CacheDrivenTask::~CacheDrivenTask() = default;

void CacheDrivenTask::process(const PageInfo& pageInfo,
                              AbstractFilterDataCollector* collector,
                              const ImageTransformation& xform,
                              const QPolygonF& contentRectPhys) {
  if (auto* geometryCollector = dynamic_cast<GeometryCollector*>(collector)) {
    geometryCollector->setGeometry(xform, contentRectPhys);
    return;
  }

  const Params params(m_settings->getParams(pageInfo.id()));
  ImageTransformation newXform(xform);
  newXform.postScaleToDpi(params.outputDpi());

  if (auto* statusCollector = dynamic_cast<OutputStatusCollector*>(collector)) {
    statusCollector->setUpToDate(isOutputUpToDate(pageInfo, newXform, contentRectPhys));
    return;
  }

  if (auto* thumbCol = dynamic_cast<ThumbnailCollector*>(collector)) {
    if (!isOutputUpToDate(pageInfo, newXform, contentRectPhys)) {
      thumbCol->processThumbnail(std::unique_ptr<QGraphicsItem>(new IncompleteThumbnail(
          thumbCol->thumbnailCache(), thumbCol->maxLogicalThumbSize(), pageInfo.imageId(), newXform)));
    } else {
      const ImageTransformation outXform(newXform.resultingRect(), params.outputDpi());
      const QString outFilePath(m_outFileNameGen.filePathFor(pageInfo.id()));

      thumbCol->processThumbnail(std::unique_ptr<QGraphicsItem>(
          new Thumbnail(thumbCol->thumbnailCache(), thumbCol->maxLogicalThumbSize(), ImageId(outFilePath), outXform)));
    }
  }
}  // CacheDrivenTask::process

bool CacheDrivenTask::isOutputUpToDate(const PageInfo& pageInfo,
                                       const ImageTransformation& xform,
                                       const QPolygonF& contentRectPhys) const {
  const std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(pageInfo.id()));
  if (!storedOutputParams) {
    return false;
  }

  const Params params(m_settings->getParams(pageInfo.id()));
  const OutputGenerator generator(xform, contentRectPhys);
  const OutputImageParams newOutputImageParams(
      generator.outputImageSize(), generator.outputContentRect(), xform, params.outputDpi(), params.colorParams(),
      params.splittingOptions(), params.dewarpingOptions(), params.distortionModel(), params.depthPerception(),
      params.despeckleLevel(), params.pictureShapeOptions(), m_settings->getOutputProcessingParams(pageInfo.id()),
      params.isBlackOnWhite());
  return storedOutputParams->isUpToDate(
      newOutputImageParams, m_settings->pictureZonesForPage(pageInfo.id()), m_settings->fillZonesForPage(pageInfo.id()),
      QFileInfo(pageInfo.id().imageId().filePath()), QFileInfo(m_outFileNameGen.filePathFor(pageInfo.id())),
      m_outFileNameGen.outDir(), RenderParams(params.colorParams(), params.splittingOptions()));
}  // CacheDrivenTask::isOutputUpToDate
}  // namespace output
