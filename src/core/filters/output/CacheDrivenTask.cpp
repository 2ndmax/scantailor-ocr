// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "CacheDrivenTask.h"

#include <QDir>
#include <QFileInfo>
#include <utility>

#include "FillZoneComparator.h"
#include "GeometryCollector.h"
#include "IncompleteThumbnail.h"
#include "OutputGenerator.h"
#include "OutputParams.h"
#include "OutputStatusCollector.h"
#include "PageInfo.h"
#include "Params.h"
#include "PictureZoneComparator.h"
#include "RenderParams.h"
#include "Settings.h"
#include "Thumbnail.h"
#include "Utils.h"
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
  if (!storedOutputParams->outputImageParams().matches(newOutputImageParams)) {
    return false;
  }

  const ZoneSet newPictureZones(m_settings->pictureZonesForPage(pageInfo.id()));
  if (!PictureZoneComparator::equal(storedOutputParams->pictureZones(), newPictureZones)) {
    return false;
  }
  const ZoneSet newFillZones(m_settings->fillZonesForPage(pageInfo.id()));
  if (!FillZoneComparator::equal(storedOutputParams->fillZones(), newFillZones)) {
    return false;
  }

  const QFileInfo sourceFileInfo(pageInfo.id().imageId().filePath());
  if (!storedOutputParams->sourceFileParams().matches(OutputFileParams(sourceFileInfo))) {
    return false;
  }

  const QFileInfo outFileInfo(m_outFileNameGen.filePathFor(pageInfo.id()));
  const RenderParams renderParams(params.colorParams(), params.splittingOptions());
  if (!renderParams.splitOutput()) {
    return outFileInfo.exists() && storedOutputParams->outputFileParams().matches(OutputFileParams(outFileInfo));
  }

  const QString outDir = m_outFileNameGen.outDir();
  const QFileInfo foregroundFileInfo(QDir(Utils::foregroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
  const QFileInfo backgroundFileInfo(QDir(Utils::backgroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
  if (!foregroundFileInfo.exists() || !backgroundFileInfo.exists()) {
    return false;
  }
  if (!storedOutputParams->foregroundFileParams().matches(OutputFileParams(foregroundFileInfo))
      || !storedOutputParams->backgroundFileParams().matches(OutputFileParams(backgroundFileInfo))) {
    return false;
  }
  if (renderParams.originalBackground()) {
    const QFileInfo originalBackgroundFileInfo(
        QDir(Utils::originalBackgroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
    if (!originalBackgroundFileInfo.exists()
        || !storedOutputParams->originalBackgroundFileParams().matches(OutputFileParams(originalBackgroundFileInfo))) {
      return false;
    }
  }
  return true;
}  // CacheDrivenTask::isOutputUpToDate
}  // namespace output
