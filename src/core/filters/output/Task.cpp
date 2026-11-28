// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"
#include "TaskResult.h"

#include <ImageLoadErrors.h>
#include <TiffWriter.h>
#include <UnitsProvider.h>

#include <QCoreApplication>
#include <QDir>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "BatchProcessingResult.h"
#include "DebugImagesImpl.h"
#include "DespeckleState.h"
#include "DespeckleVisualization.h"
#include "Dpm.h"
#include "Filter.h"
#include "FilterData.h"
#include "ImageLoader.h"
#include "OutputGenerator.h"
#include "OutputImageBuilder.h"
#include "OutputImageWithForeground.h"
#include "OutputImageWithOriginalBackground.h"
#include "RenderParams.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "ThumbnailPixmapCache.h"
#include "Utils.h"

using namespace imageproc;
using namespace dewarping;

namespace output {
namespace {
bool ensureOutputSubdir(const QString& baseOutDir, const QString& subdir) {
  return QDir(baseOutDir).exists() && QDir().mkpath(subdir);
}

bool isUnchanged(const QFileInfo& fileInfo, const OutputFileParams& storedFileParams) {
  return fileInfo.exists() && storedFileParams.matches(OutputFileParams(fileInfo));
}

bool loadBinaryMask(const QString& filePath, const QSize& expectedSize, BinaryImage& img) {
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    img = BinaryImage(ImageLoader::load(file, 0));
  }
  return !img.isNull() && img.size() == expectedSize;
}

bool loadSplitCachedOutput(const QString& foregroundFilePath,
                           const QString& backgroundFilePath,
                           const QString& originalBackgroundFilePath,
                           const bool loadOriginalBackground,
                           QImage& outImg) {
  OutputImageBuilder imageBuilder;
  QFile foregroundFile(foregroundFilePath);
  if (foregroundFile.open(QIODevice::ReadOnly)) {
    imageBuilder.setForegroundImage(ImageLoader::load(foregroundFile, 0));
  }
  QFile backgroundFile(backgroundFilePath);
  if (backgroundFile.open(QIODevice::ReadOnly)) {
    imageBuilder.setBackgroundImage(ImageLoader::load(backgroundFile, 0));
  }
  if (loadOriginalBackground) {
    QFile originalBackgroundFile(originalBackgroundFilePath);
    if (originalBackgroundFile.open(QIODevice::ReadOnly)) {
      imageBuilder.setOriginalBackgroundImage(ImageLoader::load(originalBackgroundFile, 0));
    }
  }
  try {
    outImg = *imageBuilder.build();
  } catch (const std::invalid_argument&) {
    // A layer file couldn't be read: outImg stays null, so the page is processed anew.
  }
  return !outImg.isNull();
}

void cleanupUnusedSplitFiles(const RenderParams& renderParams,
                             const QString& originalBackgroundFilePath,
                             const QString& foregroundFilePath,
                             const QString& backgroundFilePath) {
  if (!renderParams.originalBackground()) {
    QFile::remove(originalBackgroundFilePath);
  }
  if (!renderParams.splitOutput()) {
    QFile::remove(foregroundFilePath);
    QFile::remove(backgroundFilePath);
  }
}

bool writeMaskFile(const QString& outDir,
                   const QString& maskDir,
                   const QString& filePath,
                   const BinaryImage& maskImg,
                   const TiffCompression& tiffCompression) {
  return ensureOutputSubdir(outDir, maskDir) && TiffWriter::writeImage(filePath, maskImg.toQImage(), tiffCompression);
}
}  // namespace

struct Task::OutputPaths {
  QString outDir;
  QFileInfo sourceFileInfo;
  QString outFilePath;
  QFileInfo outFileInfo;
  QString foregroundDir;
  QString backgroundDir;
  QString originalBackgroundDir;
  QString foregroundFilePath;
  QString backgroundFilePath;
  QString originalBackgroundFilePath;
  QString automaskDir;
  QString automaskFilePath;
  QFileInfo automaskFileInfo;
  QString specklesDir;
  QString specklesFilePath;
  QFileInfo specklesFileInfo;
};

Task::Task(std::shared_ptr<Filter> filter,
           std::shared_ptr<Settings> settings,
           std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
           const PageId& pageId,
           const OutputFileNameGenerator& outFileNameGen,
           const ImageViewTab lastTab,
           const ProcessingOptions& processingOptions,
           const bool batch,
           const bool debug)
    : m_filter(std::move(filter)),
      m_settings(std::move(settings)),
      m_thumbnailCache(std::move(thumbnailCache)),
      m_pageId(pageId),
      m_outFileNameGen(outFileNameGen),
      m_lastTab(lastTab),
      m_processingOptions(processingOptions),
      m_batchProcessing(batch),
      m_debug(debug) {
  if (debug) {
    m_dbg = std::make_unique<DebugImagesImpl>();
  }
}

Task::~Task() = default;

Task::OutputPaths Task::resolveOutputPaths() const {
  OutputPaths paths;
  paths.outDir = m_outFileNameGen.outDir();
  paths.sourceFileInfo = QFileInfo(m_pageId.imageId().filePath());
  paths.outFilePath = m_outFileNameGen.filePathFor(m_pageId);
  paths.outFileInfo = QFileInfo(paths.outFilePath);
  paths.foregroundDir = Utils::foregroundDir(paths.outDir);
  paths.backgroundDir = Utils::backgroundDir(paths.outDir);
  paths.originalBackgroundDir = Utils::originalBackgroundDir(paths.outDir);
  paths.foregroundFilePath = QDir(paths.foregroundDir).absoluteFilePath(paths.outFileInfo.fileName());
  paths.backgroundFilePath = QDir(paths.backgroundDir).absoluteFilePath(paths.outFileInfo.fileName());
  paths.originalBackgroundFilePath = QDir(paths.originalBackgroundDir).absoluteFilePath(paths.outFileInfo.fileName());
  paths.automaskDir = Utils::automaskDir(paths.outDir);
  paths.automaskFilePath = QDir(paths.automaskDir).absoluteFilePath(paths.outFileInfo.fileName());
  paths.automaskFileInfo = QFileInfo(paths.automaskFilePath);
  paths.specklesDir = Utils::specklesDir(paths.outDir);
  paths.specklesFilePath = QDir(paths.specklesDir).absoluteFilePath(paths.outFileInfo.fileName());
  paths.specklesFileInfo = QFileInfo(paths.specklesFilePath);
  return paths;
}

void Task::resetAutoPictureZonesIfShapeChanged(const Params& params) const {
  const std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));
  if (storedOutputParams
      && storedOutputParams->outputImageParams().getPictureShapeOptions() != params.pictureShapeOptions()) {
    // if picture shape options changed, reset auto picture zones
    OutputProcessingParams outputProcessingParams = m_settings->getOutputProcessingParams(m_pageId);
    outputProcessingParams.setAutoZonesFound(false);
    m_settings->setOutputProcessingParams(m_pageId, outputProcessingParams);
  }
}

bool Task::isCacheUpToDate(const OutputPaths& paths,
                           const OutputImageParams& newOutputImageParams,
                           const ZoneSet& newPictureZones,
                           const ZoneSet& newFillZones,
                           const RenderParams& renderParams,
                           const bool needPictureEditor,
                           const bool needSpecklesImage) const {
  const std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));
  if (!storedOutputParams) {
    return false;
  }
  if (!storedOutputParams->isUpToDate(newOutputImageParams, newPictureZones, newFillZones, paths.sourceFileInfo,
                                      paths.outFileInfo, paths.outDir, renderParams)) {
    return false;
  }
  if (needPictureEditor && !isUnchanged(paths.automaskFileInfo, storedOutputParams->automaskFileParams())) {
    return false;
  }
  if (needSpecklesImage && !isUnchanged(paths.specklesFileInfo, storedOutputParams->specklesFileParams())) {
    return false;
  }
  return true;
}

bool Task::loadCachedOutputs(const OutputPaths& paths,
                             const RenderParams& renderParams,
                             const bool needPictureEditor,
                             const bool needSpecklesImage,
                             QImage& outImg,
                             BinaryImage& automaskImg,
                             BinaryImage& specklesImg) const {
  QFile outFile(paths.outFilePath);
  if (outFile.open(QIODevice::ReadOnly)) {
    outImg = ImageLoader::load(outFile, 0);
  }
  if (outImg.isNull() && renderParams.splitOutput()) {
    loadSplitCachedOutput(paths.foregroundFilePath, paths.backgroundFilePath, paths.originalBackgroundFilePath,
                          renderParams.originalBackground(), outImg);
  }
  if (outImg.isNull()) {
    return false;
  }
  if (needPictureEditor && !loadBinaryMask(paths.automaskFilePath, outImg.size(), automaskImg)) {
    return false;
  }
  // A file of another size (e.g. copied back from an earlier run) would be read outside.
  if (needSpecklesImage && !loadBinaryMask(paths.specklesFilePath, outImg.size(), specklesImg)) {
    return false;
  }
  return true;
}

void Task::updateDistortionAndOutputParams(const DistortionModel& distortionModel,
                                           Params& params,
                                           OutputImageParams& newOutputImageParams) const {
  params = m_settings->getParams(m_pageId);

  const DewarpingMode dewarpingMode = params.dewarpingOptions().dewarpingMode();
  // In manual mode only the width correction can have changed.
  const bool widthCorrectionChanged
      = (dewarpingMode == MANUAL)
        && !distortionModel.widthCorrection().matches(params.distortionModel().widthCorrection());
  if ((params.dewarpingOptions().findsCurvesItself() || widthCorrectionChanged) && distortionModel.isValid()) {
    // A new distortion model was generated.
    // We need to save it to be able to modify it manually.
    // Only this field: writing back all of params would undo changes the user made to the
    // page meanwhile in the GUI thread.
    m_settings->setDistortionModel(m_pageId, distortionModel);
    params = m_settings->getParams(m_pageId);
    newOutputImageParams.setDistortionModel(distortionModel);
  }

  // Saving refreshed params and output processing params.
  newOutputImageParams.setBlackOnWhite(m_settings->getParams(m_pageId).isBlackOnWhite());
  newOutputImageParams.setOutputProcessingParams(m_settings->getOutputProcessingParams(m_pageId));
}

std::unique_ptr<OutputImage> Task::generateOutputImage(const TaskStatus& status,
                                                       const FilterData& data,
                                                       const OutputGenerator& generator,
                                                       ZoneSet& newPictureZones,
                                                       const ZoneSet& newFillZones,
                                                       const bool writeAutomask,
                                                       const bool writeSpecklesFile,
                                                       Params& params,
                                                       OutputImageParams& newOutputImageParams,
                                                       BinaryImage& automaskImg,
                                                       BinaryImage& specklesImg) const {
  automaskImg = BinaryImage();
  specklesImg = BinaryImage();

  // OutputGenerator will write a new distortion model
  // there, if the method finds the curves itself (see DewarpingOptions::findsCurvesItself()).
  DistortionModel distortionModel;
  if (params.dewarpingOptions().dewarpingMode() == MANUAL) {
    distortionModel = params.distortionModel();
  }

  std::unique_ptr<OutputImage> outputImage = generator.process(
      status, data, newPictureZones, newFillZones, distortionModel, params.depthPerception(),
      writeAutomask ? &automaskImg : nullptr, writeSpecklesFile ? &specklesImg : nullptr, m_dbg.get(), m_pageId,
      m_settings,
      /*takeOverBlackOnWhite=*/m_processingOptions.blackOnWhiteDetection && m_processingOptions.blackOnWhiteAtOutput);

  updateDistortionAndOutputParams(distortionModel, params, newOutputImageParams);
  return outputImage;
}

bool Task::writeSplitOutputs(const OutputPaths& paths,
                             const RenderParams& renderParams,
                             const OutputImage* outputImage) const {
  if (!renderParams.splitOutput()) {
    return true;
  }

  bool ok = true;
  const auto* outputImageWithForeground = dynamic_cast<const OutputImageWithForeground*>(outputImage);
  if (!outputImageWithForeground) {
    // Report instead of crashing if the generator didn't produce the split layers.
    ImageLoadErrorReporter::instance().reportWriteFailure(
        paths.foregroundFilePath,
        QStringList(QCoreApplication::translate("output::Task", "The split output layers could not be created.")));
    ok = false;
  } else if (!ensureOutputSubdir(paths.outDir, paths.foregroundDir)
             || !ensureOutputSubdir(paths.outDir, paths.backgroundDir)
             || !TiffWriter::writeImage(paths.foregroundFilePath, outputImageWithForeground->getForegroundImage(),
                                        m_processingOptions.tiffCompression)
             || !TiffWriter::writeImage(paths.backgroundFilePath, outputImageWithForeground->getBackgroundImage(),
                                        m_processingOptions.tiffCompression)) {
    ok = false;
  }

  if (renderParams.originalBackground()) {
    const auto* outputImageWithOrigBg = dynamic_cast<const OutputImageWithOriginalBackground*>(outputImage);
    if (!outputImageWithOrigBg) {
      ImageLoadErrorReporter::instance().reportWriteFailure(
          paths.originalBackgroundFilePath,
          QStringList(
              QCoreApplication::translate("output::Task", "The original background layer could not be created.")));
      ok = false;
    } else if (!ensureOutputSubdir(paths.outDir, paths.originalBackgroundDir)
               || !TiffWriter::writeImage(paths.originalBackgroundFilePath,
                                          outputImageWithOrigBg->getOriginalBackgroundImage(),
                                          m_processingOptions.tiffCompression)) {
      ok = false;
    }
  }
  return ok;
}

bool Task::writeMainOutputImage(const QString& outFilePath, QImage& outImg) {
  if (!TiffWriter::writeImage(outFilePath, outImg, m_processingOptions.tiffCompression)) {
    return false;
  }
  deleteMutuallyExclusiveOutputFiles();
  // Show the stored file rather than the computed image, the same as when the page is
  // shown later without processing. With lossy compression such as JPEG they differ.
  QFile outFile(outFilePath);
  if (outFile.open(QIODevice::ReadOnly)) {
    const QImage storedImg = ImageLoader::load(outFile, 0);
    if (!storedImg.isNull() && (storedImg.size() == outImg.size())) {
      outImg = storedImg;
    }
  }
  return true;
}

void Task::persistOutputParams(const OutputPaths& paths,
                               const RenderParams& renderParams,
                               const OutputImageParams& newOutputImageParams,
                               const ZoneSet& newPictureZones,
                               const ZoneSet& newFillZones,
                               const bool writeAutomask,
                               const bool writeSpecklesFile,
                               const bool invalidateParams) const {
  if (invalidateParams) {
    m_settings->removeOutputParams(m_pageId);
    return;
  }

  // Note that we can't reuse *_file_info objects
  // as we've just overwritten those files.
  const OutputParams outParams(
      newOutputImageParams, OutputFileParams(paths.sourceFileInfo), OutputFileParams(QFileInfo(paths.outFilePath)),
      renderParams.splitOutput() ? OutputFileParams(QFileInfo(paths.foregroundFilePath)) : OutputFileParams(),
      renderParams.splitOutput() ? OutputFileParams(QFileInfo(paths.backgroundFilePath)) : OutputFileParams(),
      renderParams.originalBackground() ? OutputFileParams(QFileInfo(paths.originalBackgroundFilePath))
                                        : OutputFileParams(),
      writeAutomask ? OutputFileParams(QFileInfo(paths.automaskFilePath)) : OutputFileParams(),
      writeSpecklesFile ? OutputFileParams(QFileInfo(paths.specklesFilePath)) : OutputFileParams(), newPictureZones,
      newFillZones);

  m_settings->setOutputParams(m_pageId, outParams);
}

void Task::writeOutputs(const OutputPaths& paths,
                        const RenderParams& renderParams,
                        const OutputImageParams& newOutputImageParams,
                        const ZoneSet& newPictureZones,
                        const ZoneSet& newFillZones,
                        const bool writeAutomask,
                        const bool writeSpecklesFile,
                        std::unique_ptr<OutputImage> outputImage,
                        QImage& outImg,
                        BinaryImage& automaskImg,
                        BinaryImage& specklesImg) {
  bool invalidateParams = !writeSplitOutputs(paths, renderParams, outputImage.get());
  outImg = *outputImage;
  outputImage.reset();

  cleanupUnusedSplitFiles(renderParams, paths.originalBackgroundFilePath, paths.foregroundFilePath,
                          paths.backgroundFilePath);

  if (!writeMainOutputImage(paths.outFilePath, outImg)) {
    invalidateParams = true;
  }

  if (writeSpecklesFile && specklesImg.isNull()) {
    // Even if despeckling didn't actually take place, we still need
    // to write an empty speckles file. Making it a special case
    // is simply not worth it.
    BinaryImage(outImg.size(), WHITE).swap(specklesImg);
  }

  if (writeAutomask
      && !writeMaskFile(paths.outDir, paths.automaskDir, paths.automaskFilePath, automaskImg,
                        m_processingOptions.tiffCompression)) {
    invalidateParams = true;
  }
  if (writeSpecklesFile
      && !writeMaskFile(paths.outDir, paths.specklesDir, paths.specklesFilePath, specklesImg,
                        m_processingOptions.tiffCompression)) {
    invalidateParams = true;
  }

  persistOutputParams(paths, renderParams, newOutputImageParams, newPictureZones, newFillZones, writeAutomask,
                      writeSpecklesFile, invalidateParams);

  if (m_thumbnailCache) {
    m_thumbnailCache->recreateThumbnail(ImageId(paths.outFilePath), outImg);
  }
}

FilterResultPtr Task::buildResult(const Params& params,
                                  const ImageTransformation& newXform,
                                  const QRect& outputContentRect,
                                  const QImage& origImage,
                                  const QImage& outImg,
                                  const BinaryImage& automaskImg,
                                  const BinaryImage& specklesImg) {
  if (m_batchProcessing) {
    return std::make_shared<BatchProcessingResult>(m_filter, m_pageId);
  }

  const DespeckleState despeckleState(outImg, specklesImg, params.despeckleLevel(), params.outputDpi());

  DespeckleVisualization despeckleVisualization;
  if (m_lastTab == TAB_DESPECKLING) {
    // Because constructing DespeckleVisualization takes a noticeable
    // amount of time, we only do it if we are sure we'll need it.
    // Otherwise it will get constructed on demand.
    despeckleVisualization = despeckleState.visualize();
  }
  return std::make_shared<TaskResult>(m_filter, m_settings, std::move(m_dbg), params, newXform, outputContentRect,
                                      m_pageId, origImage, outImg, automaskImg, despeckleState, despeckleVisualization,
                                      m_debug);
}

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys) {
  status.throwIfCancelled();

  Params params = m_settings->getParams(m_pageId);
  const RenderParams renderParams(params.colorParams(), params.splittingOptions());

  ImageTransformation newXform(data.xform());
  newXform.postScaleToDpi(params.outputDpi());

  const OutputPaths paths = resolveOutputPaths();
  const bool needPictureEditor = renderParams.mixedOutput() && !m_batchProcessing;
  const bool needSpecklesImage
      = ((params.despeckleLevel() != .0) && renderParams.needBinarization() && !m_batchProcessing);

  resetAutoPictureZonesIfShapeChanged(params);

  const OutputGenerator generator(newXform, contentRectPhys);
  OutputImageParams newOutputImageParams(generator.outputImageSize(), generator.outputContentRect(), newXform,
                                         params.outputDpi(), params.colorParams(), params.splittingOptions(),
                                         params.dewarpingOptions(), params.distortionModel(), params.depthPerception(),
                                         params.despeckleLevel(), params.pictureShapeOptions(),
                                         m_settings->getOutputProcessingParams(m_pageId), params.isBlackOnWhite());

  ZoneSet newPictureZones(m_settings->pictureZonesForPage(m_pageId));
  const ZoneSet newFillZones(m_settings->fillZonesForPage(m_pageId));

  QImage outImg;
  BinaryImage automaskImg;
  BinaryImage specklesImg;
  const bool needReprocess = !isCacheUpToDate(paths, newOutputImageParams, newPictureZones, newFillZones, renderParams,
                                              needPictureEditor, needSpecklesImage)
                             || !loadCachedOutputs(paths, renderParams, needPictureEditor, needSpecklesImage, outImg,
                                                   automaskImg, specklesImg);

  if (needReprocess) {
    // Even in batch processing mode we should still write automask, because it
    // will be needed when we view the results back in interactive mode.
    // The same applies even more to speckles file, as we need it not only
    // for visualization purposes, but also for re-doing despeckling at
    // different levels without going through the whole output generation process.
    const bool writeAutomask = renderParams.mixedOutput();
    const bool writeSpecklesFile = ((params.despeckleLevel() != .0) && renderParams.needBinarization());

    std::unique_ptr<OutputImage> outputImage
        = generateOutputImage(status, data, generator, newPictureZones, newFillZones, writeAutomask, writeSpecklesFile,
                              params, newOutputImageParams, automaskImg, specklesImg);
    writeOutputs(paths, renderParams, newOutputImageParams, newPictureZones, newFillZones, writeAutomask,
                 writeSpecklesFile, std::move(outputImage), outImg, automaskImg, specklesImg);
  }

  return buildResult(params, newXform, generator.outputContentRect(), data.origImage(), outImg, automaskImg,
                     specklesImg);
}  // Task::process

/**
 * Delete output files mutually exclusive to m_pageId.
 */
void Task::deleteMutuallyExclusiveOutputFiles() {
  std::vector<PageId> exclusivePages;
  switch (m_pageId.subPage()) {
    case PageId::SINGLE_PAGE:
      exclusivePages.emplace_back(m_pageId.imageId(), PageId::LEFT_PAGE);
      exclusivePages.emplace_back(m_pageId.imageId(), PageId::RIGHT_PAGE);
      break;
    case PageId::LEFT_PAGE:
    case PageId::RIGHT_PAGE:
      exclusivePages.emplace_back(m_pageId.imageId(), PageId::SINGLE_PAGE);
      break;
  }

  const QStringList dirs(Utils::outputFileDirs(m_outFileNameGen.outDir()));
  for (const PageId& page : exclusivePages) {
    m_outFileNameGen.removeFiles(page, dirs);
    if (m_thumbnailCache) {
      m_thumbnailCache->removeThumbnails(ImageId(m_outFileNameGen.filePathFor(page)));
    }
  }
}

}  // namespace output
