// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Task.h"
#include "TaskResult.h"

#include <UnitsProvider.h>
#include <ImageLoadErrors.h>
#include <TiffWriter.h>

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

FilterResultPtr Task::process(const TaskStatus& status, const FilterData& data, const QPolygonF& contentRectPhys) {
  status.throwIfCancelled();

  Params params = m_settings->getParams(m_pageId);

  RenderParams renderParams(params.colorParams(), params.splittingOptions());

  ImageTransformation newXform(data.xform());
  newXform.postScaleToDpi(params.outputDpi());

  const QFileInfo sourceFileInfo(m_pageId.imageId().filePath());
  const QString outFilePath(m_outFileNameGen.filePathFor(m_pageId));
  const QFileInfo outFileInfo(outFilePath);
  const QString foregroundDir(Utils::foregroundDir(m_outFileNameGen.outDir()));
  const QString backgroundDir(Utils::backgroundDir(m_outFileNameGen.outDir()));
  const QString originalBackgroundDir(Utils::originalBackgroundDir(m_outFileNameGen.outDir()));
  const QString foregroundFilePath(QDir(foregroundDir).absoluteFilePath(outFileInfo.fileName()));
  const QString backgroundFilePath(QDir(backgroundDir).absoluteFilePath(outFileInfo.fileName()));
  const QString originalBackgroundFilePath(QDir(originalBackgroundDir).absoluteFilePath(outFileInfo.fileName()));

  const QString automaskDir(Utils::automaskDir(m_outFileNameGen.outDir()));
  const QString automaskFilePath(QDir(automaskDir).absoluteFilePath(outFileInfo.fileName()));
  QFileInfo automaskFileInfo(automaskFilePath);

  const QString specklesDir(Utils::specklesDir(m_outFileNameGen.outDir()));
  const QString specklesFilePath(QDir(specklesDir).absoluteFilePath(outFileInfo.fileName()));
  QFileInfo specklesFileInfo(specklesFilePath);

  const bool needPictureEditor = renderParams.mixedOutput() && !m_batchProcessing;
  const bool needSpecklesImage
      = ((params.despeckleLevel() != .0) && renderParams.needBinarization() && !m_batchProcessing);

  {
    std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));
    if (storedOutputParams != nullptr) {
      if (storedOutputParams->outputImageParams().getPictureShapeOptions() != params.pictureShapeOptions()) {
        // if picture shape options changed, reset auto picture zones
        OutputProcessingParams outputProcessingParams = m_settings->getOutputProcessingParams(m_pageId);
        outputProcessingParams.setAutoZonesFound(false);
        m_settings->setOutputProcessingParams(m_pageId, outputProcessingParams);
      }
    }
  }

  const OutputGenerator generator(newXform, contentRectPhys);

  OutputImageParams newOutputImageParams(generator.outputImageSize(), generator.outputContentRect(), newXform,
                                         params.outputDpi(), params.colorParams(), params.splittingOptions(),
                                         params.dewarpingOptions(), params.distortionModel(), params.depthPerception(),
                                         params.despeckleLevel(), params.pictureShapeOptions(),
                                         m_settings->getOutputProcessingParams(m_pageId), params.isBlackOnWhite());

  ZoneSet newPictureZones(m_settings->pictureZonesForPage(m_pageId));
  const ZoneSet newFillZones(m_settings->fillZonesForPage(m_pageId));

  // A file written with the stored output params that is still there and unchanged.
  const auto isUnchanged = [](const QFileInfo& fileInfo, const OutputFileParams& storedFileParams) {
    return fileInfo.exists() && storedFileParams.matches(OutputFileParams(fileInfo));
  };
  bool needReprocess = false;
  {
    const std::unique_ptr<OutputParams> storedOutputParams(m_settings->getOutputParams(m_pageId));
    needReprocess
        = !storedOutputParams
          || !storedOutputParams->isUpToDate(newOutputImageParams, newPictureZones, newFillZones, sourceFileInfo,
                                             outFileInfo, m_outFileNameGen.outDir(), renderParams)
          || (needPictureEditor && !isUnchanged(automaskFileInfo, storedOutputParams->automaskFileParams()))
          || (needSpecklesImage && !isUnchanged(specklesFileInfo, storedOutputParams->specklesFileParams()));
  }

  QImage outImg;
  BinaryImage automaskImg;
  BinaryImage specklesImg;

  if (!needReprocess) {
    QFile outFile(outFilePath);
    if (outFile.open(QIODevice::ReadOnly)) {
      outImg = ImageLoader::load(outFile, 0);
    }
    if (outImg.isNull() && renderParams.splitOutput()) {
      OutputImageBuilder imageBuilder;
      QFile foregroundFile(foregroundFilePath);
      if (foregroundFile.open(QIODevice::ReadOnly)) {
        imageBuilder.setForegroundImage(ImageLoader::load(foregroundFile, 0));
      }
      QFile backgroundFile(backgroundFilePath);
      if (backgroundFile.open(QIODevice::ReadOnly)) {
        imageBuilder.setBackgroundImage(ImageLoader::load(backgroundFile, 0));
      }
      if (renderParams.originalBackground()) {
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
    }
    needReprocess = outImg.isNull();

    if (needPictureEditor && !needReprocess) {
      QFile automaskFile(automaskFilePath);
      if (automaskFile.open(QIODevice::ReadOnly)) {
        automaskImg = BinaryImage(ImageLoader::load(automaskFile, 0));
      }
      needReprocess = automaskImg.isNull() || automaskImg.size() != outImg.size();
    }

    if (needSpecklesImage && !needReprocess) {
      QFile specklesFile(specklesFilePath);
      if (specklesFile.open(QIODevice::ReadOnly)) {
        specklesImg = BinaryImage(ImageLoader::load(specklesFile, 0));
      }
      // A file of another size (e.g. copied back from an earlier run) would be read outside.
      needReprocess = specklesImg.isNull() || specklesImg.size() != outImg.size();
    }
  }

  if (needReprocess) {
    // Even in batch processing mode we should still write automask, because it
    // will be needed when we view the results back in interactive mode.
    // The same applies even more to speckles file, as we need it not only
    // for visualization purposes, but also for re-doing despeckling at
    // different levels without going through the whole output generation process.
    const bool writeAutomask = renderParams.mixedOutput();
    const bool writeSpecklesFile = ((params.despeckleLevel() != .0) && renderParams.needBinarization());

    automaskImg = BinaryImage();
    specklesImg = BinaryImage();

    // OutputGenerator will write a new distortion model
    // there, if the method finds the curves itself (see DewarpingOptions::findsCurvesItself()).
    DistortionModel distortionModel;
    if (params.dewarpingOptions().dewarpingMode() == MANUAL) {
      distortionModel = params.distortionModel();
    }

    bool invalidateParams = false;
    {
      std::unique_ptr<OutputImage> outputImage
          = generator.process(status, data, newPictureZones, newFillZones, distortionModel, params.depthPerception(),
                              writeAutomask ? &automaskImg : nullptr, writeSpecklesFile ? &specklesImg : nullptr,
                              m_dbg.get(), m_pageId, m_settings,
                              /*takeOverBlackOnWhite=*/m_processingOptions.blackOnWhiteDetection
                                  && m_processingOptions.blackOnWhiteAtOutput);

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

      if (renderParams.splitOutput()) {
        auto* outputImageWithForeground = dynamic_cast<OutputImageWithForeground*>(outputImage.get());

        QDir().mkdir(foregroundDir);
        QDir().mkdir(backgroundDir);
        if (!outputImageWithForeground) {
          // Report instead of crashing if the generator didn't produce the split layers.
          ImageLoadErrorReporter::instance().reportWriteFailure(
              foregroundFilePath, QStringList(QCoreApplication::translate(
                                      "output::Task", "The split output layers could not be created.")));
          invalidateParams = true;
        } else if (!TiffWriter::writeImage(foregroundFilePath, outputImageWithForeground->getForegroundImage(),
                                           m_processingOptions.tiffCompression)
                   || !TiffWriter::writeImage(backgroundFilePath, outputImageWithForeground->getBackgroundImage(),
                                              m_processingOptions.tiffCompression)) {
          invalidateParams = true;
        }

        if (renderParams.originalBackground()) {
          auto* outputImageWithOrigBg = dynamic_cast<OutputImageWithOriginalBackground*>(outputImage.get());

          QDir().mkdir(originalBackgroundDir);
          if (!outputImageWithOrigBg) {
            invalidateParams = true;
          } else if (!TiffWriter::writeImage(originalBackgroundFilePath,
                                             outputImageWithOrigBg->getOriginalBackgroundImage(),
                                             m_processingOptions.tiffCompression)) {
            invalidateParams = true;
          }
        }
      }

      outImg = *outputImage;
    }

    if (!renderParams.originalBackground()) {
      QFile::remove(originalBackgroundFilePath);
    }
    if (!renderParams.splitOutput()) {
      QFile::remove(foregroundFilePath);
      QFile::remove(backgroundFilePath);
    }

    if (!TiffWriter::writeImage(outFilePath, outImg, m_processingOptions.tiffCompression)) {
      invalidateParams = true;
    } else {
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
    }

    if (writeSpecklesFile && specklesImg.isNull()) {
      // Even if despeckling didn't actually take place, we still need
      // to write an empty speckles file. Making it a special case
      // is simply not worth it.
      BinaryImage(outImg.size(), WHITE).swap(specklesImg);
    }

    if (writeAutomask) {
      // Note that QDir::mkdir() will fail if the parent directory,
      // that is $OUT/cache doesn't exist. We want that behaviour,
      // as otherwise when loading a project from a different machine,
      // a whole bunch of bogus directories would be created.
      QDir().mkdir(automaskDir);
      // Also note that QDir::mkdir() will fail if the directory already exists,
      // so we ignore its return value here.
      if (!TiffWriter::writeImage(automaskFilePath, automaskImg.toQImage(), m_processingOptions.tiffCompression)) {
        invalidateParams = true;
      }
    }
    if (writeSpecklesFile) {
      if (!QDir().mkpath(specklesDir)) {
        invalidateParams = true;
      } else if (!TiffWriter::writeImage(specklesFilePath, specklesImg.toQImage(),
                                         m_processingOptions.tiffCompression)) {
        invalidateParams = true;
      }
    }

    if (invalidateParams) {
      m_settings->removeOutputParams(m_pageId);
    } else {
      // Note that we can't reuse *_file_info objects
      // as we've just overwritten those files.
      const OutputParams outParams(
          newOutputImageParams, OutputFileParams(sourceFileInfo), OutputFileParams(QFileInfo(outFilePath)),
          renderParams.splitOutput() ? OutputFileParams(QFileInfo(foregroundFilePath)) : OutputFileParams(),
          renderParams.splitOutput() ? OutputFileParams(QFileInfo(backgroundFilePath)) : OutputFileParams(),
          renderParams.originalBackground() ? OutputFileParams(QFileInfo(originalBackgroundFilePath))
                                            : OutputFileParams(),
          writeAutomask ? OutputFileParams(QFileInfo(automaskFilePath)) : OutputFileParams(),
          writeSpecklesFile ? OutputFileParams(QFileInfo(specklesFilePath)) : OutputFileParams(), newPictureZones,
          newFillZones);

      m_settings->setOutputParams(m_pageId, outParams);
    }

    if (m_thumbnailCache) {
      m_thumbnailCache->recreateThumbnail(ImageId(outFilePath), outImg);
    }
  }

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
  return std::make_shared<TaskResult>(m_filter, m_settings, std::move(m_dbg), params, newXform,
                                      generator.outputContentRect(), m_pageId, data.origImage(), outImg, automaskImg,
                                      despeckleState, despeckleVisualization, m_debug);
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
