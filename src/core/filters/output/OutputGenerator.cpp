// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGenerator.h"

#include <AdjustBrightness.h>
#include <Binarize.h>
#include <BlackOnWhiteEstimator.h>
#include <ColoredDimming.h>
#include <ConnCompEraser.h>
#include <ConnectivityMap.h>
#include <Constants.h>
#include <CylindricalSurfaceDewarper.h>
#include <Despeckle.h>
#include <DewarpingPointMapper.h>
#include <DistortionModelBuilder.h>
#include <DrawOver.h>
#include <GrayRasterOp.h>
#include <Grayscale.h>
#include <InfluenceMap.h>
#include <Morphology.h>
#include <OrthogonalRotation.h>
#include <PictureDetection.h>
#include <PolygonRasterizer.h>
#include <PolynomialSurface.h>
#include <RasterDewarper.h>
#include <RasterOp.h>
#include <Scale.h>
#include <SeedFill.h>
#include <TextLineTracer.h>
#include <TopBottomEdgeTracer.h>
#include <Transform.h>
#include <WidthProfile.h>
#include <imageproc/BackgroundColorCalculator.h>
#include <imageproc/ChromaDenoiser.h>
#include <imageproc/ColorKMeans.h>
#include <imageproc/ColorSegmenter.h>
#include <imageproc/ExperimentalBinarization.h>
#include <imageproc/ImageCombination.h>
#include <imageproc/KnnDenoiser.h>
#include <imageproc/OrthogonalRotation.h>
#include <imageproc/PolygonUtils.h>
#include <imageproc/Posterizer.h>
#include <imageproc/Sharpen.h>
#include <imageproc/SkewFinder.h>
#include <imageproc/SubtractBackground.h>
#include <imageproc/WienerFilter.h>

#include <QColor>
#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QTransform>
#include <cmath>
#include <functional>
#include <optional>
#include <stdexcept>
#include <type_traits>

#include "BlackWhiteBinarization.h"
#include "ColorParams.h"
#include "DebugImages.h"
#include "DewarpingOptions.h"
#include "Dpm.h"
#include "EstimateBackground.h"
#include "FilterData.h"
#include "ForegroundType.h"
#include "ImageSizeLimit.h"
#include "OutputGeneratorProcessor.h"
#include "OutputImageBuilder.h"
#include "OutputImageOps.h"
#include "OutputMorphologyOps.h"
#include "OutputProcessingParams.h"
#include "OutputRectFinder.h"
#include "OutputZoneOps.h"
#include "Params.h"
#include "PictureLayerProperty.h"
#include "PictureShapeOptions.h"
#include "RenderParams.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "Utils.h"

using namespace imageproc;
using namespace dewarping;

namespace output {
namespace {
// Coordinates for which QRectF::toRect() can't overflow int. A huge output resolution in a
// project file gives larger ones; cut to this range, the output still exceeds ImageSizeLimit by
// far, so calcAreas() refuses it as before.
const QRectF INT_SAFE_RANGE(-(1 << 29), -(1 << 29), 1 << 30, 1 << 30);

QRect toRectInIntRange(const QRectF& rect) {
  return rect.intersected(INT_SAFE_RANGE).toRect();
}

// The size of the output image. At least 1 x 1 pixel, as there are no empty images, also for a
// page that scales to less than half a pixel.
QSize outputSizeOf(const QRect& outRect) {
  return outRect.size().expandedTo(QSize(1, 1));
}
}  // namespace

OutputGenerator::OutputGenerator(const ImageTransformation& xform, const QPolygonF& contentRectPhys)
    : m_xform(xform),
      m_outRect(toRectInIntRange(xform.resultingRect())),
      m_contentRect(toRectInIntRange(xform.transform().map(contentRectPhys).boundingRect())) {
  assert(m_outRect.topLeft() == QPoint(0, 0));

  if (!m_contentRect.isEmpty()) {
    // prevents a crash due to round error on transforming virtual coordinates to output image coordinates
    // when m_contentRect coordinates could exceed m_outRect ones by 1 px
    m_contentRect = m_contentRect.intersected(m_outRect);
    // Note that QRect::contains(<empty rect>) always returns false, so we don't use it here.
    assert(m_outRect.contains(m_contentRect.topLeft()) && m_outRect.contains(m_contentRect.bottomRight()));
  }
}

QSize OutputGenerator::outputImageSize() const {
  return outputSizeOf(m_outRect);
}

std::unique_ptr<OutputImage> OutputGenerator::process(const TaskStatus& status,
                                                      const FilterData& input,
                                                      ZoneSet& pictureZones,
                                                      const ZoneSet& fillZones,
                                                      DistortionModel& distortionModel,
                                                      const DepthPerception& depthPerception,
                                                      BinaryImage* autoPictureMask,
                                                      BinaryImage* specklesImage,
                                                      DebugImages* dbg,
                                                      const PageId& pageId,
                                                      const std::shared_ptr<Settings>& settings,
                                                      const bool takeOverBlackOnWhite) const {
  return detail::Processor(*this, pageId, settings, takeOverBlackOnWhite, input, status, dbg)
      .process(pictureZones, fillZones, distortionModel, depthPerception, autoPictureMask, specklesImage);
}

detail::Processor::Processor(const OutputGenerator& generator,
                             const PageId& pageId,
                             const std::shared_ptr<Settings>& settings,
                             const bool takeOverBlackOnWhite,
                             const FilterData& input,
                             const TaskStatus& status,
                             DebugImages* dbg)
    : m_xform(generator.m_xform),
      m_outRect(generator.m_outRect),
      m_contentRect(generator.m_contentRect),
      m_pageId(pageId),
      m_settings(settings),
      m_takeOverBlackOnWhite(takeOverBlackOnWhite),
      m_despeckleLevel(0),
      m_blank(false),
      m_blackOnWhite(true),
      m_colorOriginal(false),
      m_status(status),
      m_dbg(dbg) {
  initParams();
  calcAreas();
  if (!m_blank) {
    initFilterData(input);
  }
}

std::unique_ptr<OutputImage> detail::Processor::process(ZoneSet& pictureZones,
                                                        const ZoneSet& fillZones,
                                                        DistortionModel& distortionModel,
                                                        const DepthPerception& depthPerception,
                                                        BinaryImage* autoPictureMask,
                                                        BinaryImage* specklesImage) {
  std::unique_ptr<OutputImage> image
      = processImpl(pictureZones, fillZones, distortionModel, depthPerception, autoPictureMask, specklesImage);
  image->setDpm(m_dpi);
  return image;
}

void detail::Processor::initParams() {
  const Params params = m_settings->getParams(m_pageId);
  m_dpi = params.outputDpi();
  m_colorParams = params.colorParams();
  m_splittingOptions = params.splittingOptions();
  m_pictureShapeOptions = params.pictureShapeOptions();
  m_dewarpingOptions = params.dewarpingOptions();
  m_despeckleLevel = params.despeckleLevel();

  m_outputProcessingParams = m_settings->getOutputProcessingParams(m_pageId);

  m_renderParams = RenderParams(m_colorParams, m_splittingOptions);
}

void detail::Processor::calcAreas() {
  m_targetSize = outputSizeOf(m_outRect);
  // The limit for images also holds for the output, as it is processed the same way. Without it,
  // a high output resolution or a too low DPI of the image could make the output arbitrarily large.
  if (ImageSizeLimit::exceeds(m_targetSize)) {
    throw OutputTooLargeError(m_targetSize);
  }

  if (m_renderParams.fillOutsidePageBox()) {
    m_preCropArea = QRectF(m_outRect);
  } else if (m_renderParams.fillOffcut()) {
    m_preCropArea = m_xform.resultingPreCropArea();
  } else {
    const QPolygonF imageRectInOutputCs = m_xform.transform().map(m_xform.origRect());
    m_preCropArea = imageRectInOutputCs.intersected(QRectF(m_outRect));
  }

  m_croppedContentArea = m_preCropArea.intersected(QRectF(m_renderParams.fillMargins() ? m_contentRect : m_outRect));
  m_croppedContentRect = m_croppedContentArea.boundingRect().toRect();
  m_croppedContentArea = PolygonUtils::round(m_croppedContentArea.intersected(QRectF(m_croppedContentRect)));
  if (m_croppedContentRect.isEmpty()) {
    m_blank = true;
    return;
  }

  m_outCropArea = m_preCropArea.intersected(QRectF(m_outRect));

  // For various reasons, we need some whitespace around the content
  // area. This is the number of pixels of such whitespace.
  const int contentMargin = m_dpi.vertical() * 20 / 300;
  // The content area (in output image coordinates) extended
  // with contentMargin. Note that we prevent that extension
  // from reaching the neighboring page.
  // This is the area we are going to pass to estimateBackground().
  // estimateBackground() needs some margins around content, and
  // generally smaller margins are better, except when there is
  // some garbage that connects the content to the edge of the
  // image area.
  m_workingBoundingRect = m_preCropArea
                              .intersected(QRectF(m_croppedContentRect.adjusted(-contentMargin, -contentMargin,
                                                                                contentMargin, contentMargin)))
                              .boundingRect()
                              .toRect();
  m_contentRectInWorkingCs = m_croppedContentRect.translated(-m_workingBoundingRect.topLeft());
  // Whether and how a page is dewarped must not depend on "Fill margins". So the text lines are always traced in
  // the content box and its margin, which is the working area when the margins are filled. Without a content box
  // there is only the whole page to trace.
  const QRect contentBox = m_preCropArea.intersected(QRectF(m_contentRect)).boundingRect().toRect();
  const QRect tracedBox = contentBox.isEmpty() ? m_croppedContentRect : contentBox;
  const QRect tracingRect
      = m_preCropArea
            .intersected(QRectF(tracedBox.adjusted(-contentMargin, -contentMargin, contentMargin, contentMargin)))
            .boundingRect()
            .toRect()
        & m_workingBoundingRect;
  m_tracingRectInWorkingCs = tracingRect.translated(-m_workingBoundingRect.topLeft());
  m_contentBoxInTracingCs = tracedBox.translated(-tracingRect.topLeft());
  m_contentAreaInWorkingCs = m_croppedContentArea.translated(-m_workingBoundingRect.topLeft());
  m_outCropAreaInWorkingCs = m_outCropArea.translated(-m_workingBoundingRect.topLeft());
  m_preCropAreaInOriginalCs = m_xform.transformBack().map(m_preCropArea);
  m_contentAreaInOriginalCs = m_xform.transformBack().map(m_croppedContentArea);
  m_outCropAreaInOriginalCs = m_xform.transformBack().map(m_outCropArea);
}


void fillMarginsInPlace(QImage& image, const BinaryImage& contentMask, const QColor& color) {
  detail::fillMarginsInPlace(image, contentMask, color);
}

std::vector<QRect> findRectAreas(const BinaryImage& mask, int sensitivity) {
  return detail::findRectAreas(mask, sensitivity);
}


std::unique_ptr<OutputImage> detail::Processor::buildEmptyImage() const {
  OutputImageBuilder imageBuilder;

  BinaryImage emptyImage(m_targetSize, WHITE);
  if (!m_renderParams.splitOutput()) {
    imageBuilder.setImage(emptyImage.toQImage());
  } else {
    // The split layers are cut out with applyMask(), which rejects 1-bit images,
    // so use a white 8-bit grayscale image here.
    GrayImage whiteImage(m_targetSize);
    whiteImage.fill(0xff);
    imageBuilder.setImage(whiteImage.toQImage());
    // Without a foreground type, build() would return a plain image without the split layers.
    imageBuilder.setForegroundType(getForegroundType());
    imageBuilder.setForegroundMask(emptyImage);
    if (m_renderParams.originalBackground()) {
      imageBuilder.setBackgroundMask(emptyImage);
    }
  }
  return imageBuilder.build();
}

void detail::Processor::updateBlackOnWhite(const FilterData& input) {
  bool blackOnWhite = m_settings->getParams(m_pageId).isBlackOnWhite();
  if (m_takeOverBlackOnWhite && !m_settings->getOutputProcessingParams(m_pageId).isBlackOnWhiteSetManually()) {
    if (blackOnWhite != input.isBlackOnWhite()) {
      blackOnWhite = input.isBlackOnWhite();
      // Only this field: writing back all of the params would undo changes the user made to the
      // page meanwhile in the GUI thread.
      m_settings->setBlackOnWhite(m_pageId, blackOnWhite);
    }
  }
  m_blackOnWhite = blackOnWhite;
}

void detail::Processor::initFilterData(const FilterData& input) {
  updateBlackOnWhite(input);

  m_inputGrayImage = m_blackOnWhite ? input.grayImage() : input.grayImage().inverted();
  m_inputOrigImage = input.origImage();
  if (!m_blackOnWhite) {
    m_inputOrigImage.invertPixels();
  }
  // With grayscale output, a color source is processed like a grayscale scan. Black and white
  // mode is left alone, as its color segmentation keeps colored text on purpose.
  const bool grayscaleOutput
      = m_colorParams.colorCommonOptions().isGrayscaleOutput() && (m_colorParams.colorMode() != BLACK_AND_WHITE);
  m_colorOriginal = !grayscaleOutput && !m_inputOrigImage.allGray();
}

std::unique_ptr<OutputImage> detail::Processor::processImpl(ZoneSet& pictureZones,
                                                            const ZoneSet& fillZones,
                                                            DistortionModel& distortionModel,
                                                            const DepthPerception& depthPerception,
                                                            BinaryImage* autoPictureMask,
                                                            BinaryImage* specklesImage) {
  if (m_blank) {
    if (autoPictureMask) {
      // A blank page has no picture areas. The mask is still needed for the picture zones tab.
      BinaryImage(m_targetSize, BLACK).swap(*autoPictureMask);
    }
    return buildEmptyImage();
  }

  m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
      m_colorOriginal ? m_inputOrigImage : m_inputGrayImage, m_outCropAreaInOriginalCs);

  if (m_dewarpingOptions.findsCurvesItself()
      || ((m_dewarpingOptions.dewarpingMode() == MANUAL) && distortionModel.isValid())) {
    return processWithDewarping(pictureZones, fillZones, distortionModel, depthPerception, autoPictureMask,
                                specklesImage);
  } else {
    return processWithoutDewarping(pictureZones, fillZones, autoPictureMask, specklesImage);
  }
}

std::unique_ptr<OutputImage> detail::Processor::processWithoutDewarping(ZoneSet& pictureZones,
                                                                        const ZoneSet& fillZones,
                                                                        BinaryImage* autoPictureMask,
                                                                        BinaryImage* specklesImage) {
  QImage maybeNormalized, maybeSmoothed;
  BinaryImage bwContent, bwContentMaskOutput, bwContentOutput;
  OutputImageBuilder imageBuilder;

  maybeNormalized = transformToWorkingCs(m_renderParams.normalizeIllumination());
  if (m_dbg) {
    m_dbg->add(maybeNormalized, "maybeNormalized");
  }

  applyEnhancementFilters(maybeNormalized);

  if (m_renderParams.normalizeIllumination()) {
    m_outsideBackgroundColor
        = BackgroundColorCalculator::calcDominantBackgroundColor(maybeNormalized, m_outCropAreaInWorkingCs);
  }

  m_status.throwIfCancelled();

  if (m_renderParams.binaryOutput() || m_renderParams.mixedOutput()) {
    // BW mask begin
    if (!m_renderParams.needSavitzkyGolaySmoothing()) {
      maybeSmoothed = dimColoredPixels(maybeNormalized, m_colorParams.blackWhiteOptions().getColoredDimming());
    } else {
      maybeSmoothed = detail::smoothToGrayscale(
          dimColoredPixels(maybeNormalized, m_colorParams.blackWhiteOptions().getColoredDimming()), m_dpi);
      if (m_dbg) {
        m_dbg->add(maybeSmoothed, "smoothed");
      }
      m_status.throwIfCancelled();
    }

    bwContent = binarize(maybeSmoothed, m_contentAreaInWorkingCs);
    maybeSmoothed = QImage();  // Save memory.
    if (m_dbg) {
      m_dbg->add(bwContent, "binarized_and_cropped");
    }
    m_status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(bwContent);
      m_status.throwIfCancelled();
    }
    // BW mask end
  }

  // (BW only / Color segment) begin
  if (m_renderParams.binaryOutput()) {
    // don't destroy as it's needed for color segmentation
    if (!m_renderParams.needColorSegmentation()) {
      maybeNormalized = QImage();
    }

    BinaryImage dst(m_targetSize, WHITE);
    rasterOp<RopSrc>(dst, m_croppedContentRect, bwContent, m_contentRectInWorkingCs.topLeft());
    bwContent.release();  // Save memory.

    // It's important to keep despeckling the very last operation
    // affecting the binary part of the output. That's because
    // we will be reconstructing the input to this despeckling
    // operation from the final output file.
    maybeDespeckleInPlace(dst, m_outRect, m_outRect, m_despeckleLevel, specklesImage, m_dpi);

    if (!m_renderParams.needColorSegmentation()) {
      if (!m_blackOnWhite) {
        dst.invert();
      }

      detail::applyFillZonesInPlace(dst, fillZones, m_xform.transform());

      imageBuilder.setImage(dst.toQImage());
    } else {
      QImage segmentedImage;
      {
        QImage colorImage(m_targetSize, maybeNormalized.format());
        if (maybeNormalized.format() == QImage::Format_Indexed8) {
          colorImage.setColorTable(maybeNormalized.colorTable());
        }
        colorImage.fill(Qt::white);
        drawOver(colorImage, m_croppedContentRect, maybeNormalized, m_contentRectInWorkingCs);
        maybeNormalized = QImage();

        segmentedImage = colorText(dst, colorImage);
        dst.release();
      }


      if (!m_blackOnWhite) {
        segmentedImage.invertPixels();
      }

      detail::applyFillZonesInPlace(segmentedImage, fillZones, m_xform.transform(), false);
      if (m_dbg) {
        m_dbg->add(segmentedImage, "segmented_with_fill_zones");
      }
      m_status.throwIfCancelled();

      imageBuilder.setImage(segmentedImage);
    }
    return imageBuilder.build();
  }
  // (BW only / Color segment) end

  // Mixed begin
  if (m_renderParams.mixedOutput()) {
    BinaryImage bwMask(m_workingBoundingRect.size(), BLACK);
    processPictureZones(bwMask, pictureZones, GrayImage(maybeNormalized));
    if (m_dbg) {
      m_dbg->add(bwMask, "bwMask");
    }
    m_status.throwIfCancelled();

    if (autoPictureMask) {
      if (autoPictureMask->size() != m_targetSize) {
        BinaryImage(m_targetSize).swap(*autoPictureMask);
      }
      autoPictureMask->fill(BLACK);
      rasterOp<RopSrc>(*autoPictureMask, m_croppedContentRect, bwMask, m_contentRectInWorkingCs.topLeft());
    }
    m_status.throwIfCancelled();

    modifyBinarizationMask(bwMask, bwContent, m_workingBoundingRect, pictureZones);
    detail::fillMarginsInPlace(bwMask, m_contentAreaInWorkingCs, BLACK);
    if (m_dbg) {
      m_dbg->add(bwMask, "bwMask with zones");
    }
    m_status.throwIfCancelled();

    if (m_renderParams.needBinarization()) {
      // It's important to keep despeckling the very last operation
      // affecting the binary part of the output. That's because
      // we will be reconstructing the input to this despeckling
      // operation from the final output file.
      maybeDespeckleInPlace(bwContent, m_workingBoundingRect, m_croppedContentRect, m_despeckleLevel, specklesImage,
                            m_dpi);

      if (m_renderParams.originalBackground()) {
        // The original background path combines without a mask, so keep the
        // binarized content out of picture areas, or it would be painted over the pictures.
        rasterOp<RopAnd<RopSrc, RopDst>>(bwContent, bwMask);
      }

      if (!m_renderParams.normalizeIlluminationColor()) {
        m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
            m_colorOriginal ? m_inputOrigImage : m_inputGrayImage, m_outCropAreaInOriginalCs);
        maybeNormalized = transformToWorkingCs(false);
        applyEnhancementFilters(maybeNormalized);
      }

      if (!m_renderParams.needColorSegmentation()) {
        if (m_renderParams.originalBackground()) {
          combineImages(maybeNormalized, bwContent);
        } else {
          combineImages(maybeNormalized, bwContent, bwMask);
        }
      } else {
        QImage segmentedImage = colorText(bwContent, maybeNormalized);

        if (m_renderParams.originalBackground()) {
          combineImages(maybeNormalized, segmentedImage);
        } else {
          combineImages(maybeNormalized, segmentedImage, bwMask);
        }
      }
      if (m_dbg) {
        m_dbg->add(maybeNormalized, "combined");
      }

      if (m_renderParams.originalBackground()) {
        detail::reserveBlackAndWhite(maybeNormalized, bwContent.inverted());
      } else {
        detail::reserveBlackAndWhite(maybeNormalized, bwMask.inverted());
      }
      m_status.throwIfCancelled();

      if (m_renderParams.originalBackground()) {
        bwContentOutput = BinaryImage(m_targetSize, WHITE);
        rasterOp<RopSrc>(bwContentOutput, m_croppedContentRect, bwContent, m_contentRectInWorkingCs.topLeft());
      }
      // Release only now: the original background branches above still read bwContent.
      bwContent.release();  // Save memory.
    }

    applyPhotoAdjustments(maybeNormalized, &bwMask);

    bwContentMaskOutput = BinaryImage(m_targetSize, BLACK);
    rasterOp<RopSrc>(bwContentMaskOutput, m_croppedContentRect, bwMask, m_contentRectInWorkingCs.topLeft());
  }
  // Mixed end

  if (!m_renderParams.mixedOutput()) {
    applyPhotoAdjustments(maybeNormalized, nullptr);
  }

  assert(!m_targetSize.isEmpty());
  QImage dst(m_targetSize, maybeNormalized.format());
  if (maybeNormalized.format() == QImage::Format_Indexed8) {
    dst.setColorTable(maybeNormalized.colorTable());
  }

  if (dst.isNull()) {
    // Both the constructor and setColorTable() above can leave the image null.
    throw std::bad_alloc();
  }

  if (m_renderParams.needBinarization() && !m_renderParams.originalBackground()) {
    // The image is inverted back below for light content on dark background.
    switch (m_colorParams.colorCommonOptions().getFillingColor()) {
      case FILL_WHITE:
        m_outsideBackgroundColor = m_blackOnWhite ? Qt::white : Qt::black;
        break;
      case FILL_BLACK:
        m_outsideBackgroundColor = m_blackOnWhite ? Qt::black : Qt::white;
        break;
      default:
        m_outsideBackgroundColor = Qt::white;
    }
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_WHITE) {
    m_outsideBackgroundColor = m_blackOnWhite ? Qt::white : Qt::black;
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_BLACK) {
    m_outsideBackgroundColor = m_blackOnWhite ? Qt::black : Qt::white;
  }

  detail::fillMarginsInPlace(maybeNormalized, m_contentAreaInWorkingCs, m_outsideBackgroundColor);
  dst.fill(m_outsideBackgroundColor);

  drawOver(dst, m_croppedContentRect, maybeNormalized, m_contentRectInWorkingCs);
  maybeNormalized = QImage();

  if (!m_blackOnWhite) {
    dst.invertPixels();
  }

  // applyFillZonesToMixedInPlace() re-binarizes the non-picture areas, which would
  // wipe out the original background, so paint fill zones directly in that mode.
  if (m_renderParams.mixedOutput() && m_renderParams.needBinarization() && !m_renderParams.originalBackground()) {
    detail::applyFillZonesToMixedInPlace(dst, fillZones, m_xform.transform(), bwContentMaskOutput,
                                         !m_renderParams.needColorSegmentation());
  } else {
    detail::applyFillZonesInPlace(dst, fillZones, m_xform.transform());
  }
  if (m_renderParams.originalBackground()) {
    detail::applyFillZonesToMask(bwContentOutput, fillZones, m_xform.transform());
    rasterOp<RopAnd<RopSrc, RopDst>>(bwContentOutput, bwContentMaskOutput);
  }
  if (m_dbg) {
    m_dbg->add(dst, "fillZones");
  }
  m_status.throwIfCancelled();

  if (m_renderParams.posterize() && !m_renderParams.needBinarization()) {
    dst = posterizeImage(dst);
  }

  if (m_renderParams.splitOutput()) {
    imageBuilder.setForegroundType(getForegroundType());
    if (!m_renderParams.originalBackground()) {
      imageBuilder.setForegroundMask(bwContentMaskOutput);
    } else {
      imageBuilder.setForegroundMask(bwContentOutput);
      imageBuilder.setBackgroundMask(bwContentMaskOutput.inverted());
    }
  }
  return imageBuilder.setImage(dst).build();
}

std::unique_ptr<OutputImage> detail::Processor::processWithDewarping(ZoneSet& pictureZones,
                                                                     const ZoneSet& fillZones,
                                                                     DistortionModel& distortionModel,
                                                                     const DepthPerception& depthPerception,
                                                                     BinaryImage* autoPictureMask,
                                                                     BinaryImage* specklesImage) {
  OutputImageBuilder imageBuilder;

  // The output we would get if dewarping was turned off, except always grayscale.
  // Used for automatic picture detection and binarization threshold calculation.
  // This image corresponds to the area of workingBoundingRect.
  GrayImage warpedGrayOutput;
  if (!m_renderParams.normalizeIllumination()) {
    warpedGrayOutput = transformToGray(m_inputGrayImage, m_xform.transform(), m_workingBoundingRect,
                                       OutsidePixels::assumeWeakColor(m_outsideBackgroundColor));
  } else {
    warpedGrayOutput = normalizeIlluminationGray(m_inputGrayImage, m_preCropAreaInOriginalCs, m_xform.transform(),
                                                 m_workingBoundingRect);
  }

  // Original image, but:
  // 1. In a format we can handle, that is grayscale, RGB32, ARGB32
  // 2. With illumination normalized over the content area, if required.
  // 3. With margins filled with white, if required.
  QImage normalizedOriginal;
  const QTransform workingToOrig
      = QTransform().translate(m_workingBoundingRect.left(), m_workingBoundingRect.top()) * m_xform.transformBack();
  if (!m_renderParams.normalizeIllumination()) {
    normalizedOriginal = (m_colorOriginal) ? m_inputOrigImage : m_inputGrayImage;
  } else {
    GrayImage normalizedGray = transformToGray(warpedGrayOutput, workingToOrig, m_inputOrigImage.rect(),
                                               OutsidePixels::assumeWeakColor(m_outsideBackgroundColor));
    if (!m_colorOriginal) {
      normalizedOriginal = normalizedGray;
    } else {
      normalizedOriginal = m_inputOrigImage;
      adjustBrightnessGrayscale(normalizedOriginal, normalizedGray);
      if (m_dbg) {
        m_dbg->add(normalizedOriginal, "norm_illum_color");
      }
    }

    m_outsideBackgroundColor
        = BackgroundColorCalculator::calcDominantBackgroundColor(normalizedOriginal, m_outCropAreaInOriginalCs);
  }

  m_status.throwIfCancelled();

  // Picture mask (white indicate a picture) in the same coordinates as
  // warpedGrayOutput. Only built for Mixed mode.
  BinaryImage warpedBwMask;
  if (m_renderParams.mixedOutput()) {
    warpedBwMask = BinaryImage(m_workingBoundingRect.size(), BLACK);
    processPictureZones(warpedBwMask, pictureZones, warpedGrayOutput);
    if (m_dbg) {
      m_dbg->add(warpedBwMask, "warpedBwMask");
    }
    m_status.throwIfCancelled();

    if (autoPictureMask) {
      if (autoPictureMask->size() != m_targetSize) {
        BinaryImage(m_targetSize).swap(*autoPictureMask);
      }
      autoPictureMask->fill(BLACK);
      rasterOp<RopSrc>(*autoPictureMask, m_croppedContentRect, warpedBwMask, m_contentRectInWorkingCs.topLeft());
    }
    m_status.throwIfCancelled();

    // BW mask begin
    BinaryImage warpedBwContent;
    QImage maybeSmoothed;
    if (!m_renderParams.needSavitzkyGolaySmoothing()) {
      maybeSmoothed = warpedGrayOutput;
    } else {
      maybeSmoothed = detail::smoothToGrayscale(warpedGrayOutput, m_dpi);
      if (m_dbg) {
        m_dbg->add(maybeSmoothed, "smoothed");
      }
      m_status.throwIfCancelled();
    }

    warpedBwContent = binarize(maybeSmoothed, m_contentAreaInWorkingCs);
    maybeSmoothed = QImage();  // Save memory.
    if (m_dbg) {
      m_dbg->add(warpedBwContent, "binarized_and_cropped");
    }
    m_status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(warpedBwContent);
      m_status.throwIfCancelled();
    }
    // BW mask end

    modifyBinarizationMask(warpedBwMask, warpedBwContent, m_workingBoundingRect, pictureZones);
    warpedBwContent.release();  // Save memory.
    if (m_dbg) {
      m_dbg->add(warpedBwMask, "warpedBwMask with zones");
    }
    m_status.throwIfCancelled();
  }

  // REVIEW-NOTE (2026-11): with "Normalize illumination" on, the background is estimated over the working area,
  // which is the whole page when the margins are not filled -> on a page with uneven light and a dark scan edge the
  // gray values cut out for the tracing differ, and an edge of the model can move by about a millimetre. Not fixed
  // because normalizing the traced area once more costs time and the pages measured kept their decision.
  const auto pageToTrace = [&] { return GrayImage(warpedGrayOutput.toQImage().copy(m_tracingRectInWorkingCs)); };
  const QTransform tracingToOrig
      = QTransform::fromTranslate(m_tracingRectInWorkingCs.left(), m_tracingRectInWorkingCs.top()) * workingToOrig;

  m_dewarpingOptions.setNoModelFound(false);
  if (m_dewarpingOptions.dewarpingMode() == AUTO) {
    distortionModel = buildAutoDistortionModel(pageToTrace(), tracingToOrig);
  } else if (m_dewarpingOptions.dewarpingMode() == MARGINAL) {
    distortionModel = buildMarginalDistortionModel();
  } else if (m_dewarpingOptions.dewarpingMode() == TRAPEZOID) {
    distortionModel = buildTrapezoidDistortionModel(pageToTrace(), tracingToOrig);
  }
  warpedGrayOutput = GrayImage();  // Save memory.

  QTransform rotateXform;
  QImage dewarped;
  try {
    // A stored width correction comes from an earlier run; it is measured anew below.
    distortionModel.setWidthCorrection(WidthCorrection());
    dewarped = dewarp(QTransform(), normalizedOriginal, m_xform.transform(), distortionModel, depthPerception,
                      m_outsideBackgroundColor);
    // A page reported as not dewarped stays as it is, also in its width.
    if (m_dewarpingOptions.evenOutLetterWidths() && !m_dewarpingOptions.noModelFound()) {
      evenOutLetterWidths(distortionModel, depthPerception, normalizedOriginal, dewarped);
    }

    const double deskewAngle = -findSkew(dewarped);
    rotateXform = Utils::rotate(deskewAngle, m_outRect);
    m_dewarpingOptions.setPostDeskewAngle(deskewAngle);
  } catch (const std::runtime_error&) {
    // Probably an impossible distortion model. Let's fall back to a trivial one.
    setupTrivialDistortionModel(distortionModel);
    // evenOutLetterWidths() may already have stored a correction measured with the failed model.
    distortionModel.setWidthCorrection(WidthCorrection());
    m_dewarpingOptions.setPostDeskewAngle(.0);

    dewarped = dewarp(QTransform(), normalizedOriginal, m_xform.transform(), distortionModel, depthPerception,
                      m_outsideBackgroundColor);
  }
  normalizedOriginal = QImage();  // Save memory.
  m_settings->setDewarpingOptions(m_pageId, m_dewarpingOptions);
  detail::applyAffineTransform(dewarped, rotateXform, m_outsideBackgroundColor);
  // As without dewarping, the filters work on the page in output resolution.
  applyEnhancementFilters(dewarped);
  if (m_dbg) {
    m_dbg->add(dewarped, "dewarped");
  }
  m_status.throwIfCancelled();

  // The same model domain as in dewarp(), or the zones land off their place on the dewarped page.
  auto mapper = std::make_shared<DewarpingPointMapper>(distortionModel, depthPerception.value(), m_xform.transform(),
                                                       m_outRect, rotateXform);
  const std::function<QPointF(const QPointF&)> origToOutput(
      std::bind(&DewarpingPointMapper::mapToDewarpedSpace, mapper, std::placeholders::_1));

  BinaryImage dewarpingContentAreaMask(m_inputGrayImage.size(), BLACK);
  {
    detail::fillMarginsInPlace(dewarpingContentAreaMask, m_contentAreaInOriginalCs, WHITE);
    dewarpingContentAreaMask = BinaryImage(dewarp(QTransform(), dewarpingContentAreaMask.toQImage(),
                                                  m_xform.transform(), distortionModel, depthPerception, Qt::white));
  }
  detail::applyAffineTransform(dewarpingContentAreaMask, rotateXform, WHITE);

  if (m_renderParams.binaryOutput()) {
    QImage dewarpedAndMaybeSmoothed;
    // We only do smoothing if we are going to do binarization later.
    if (!m_renderParams.needSavitzkyGolaySmoothing()) {
      dewarpedAndMaybeSmoothed = dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming());
    } else {
      dewarpedAndMaybeSmoothed = detail::smoothToGrayscale(
          dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming()), m_dpi);
      if (m_dbg) {
        m_dbg->add(dewarpedAndMaybeSmoothed, "smoothed");
      }
      m_status.throwIfCancelled();
    }

    // don't destroy as it's needed for color segmentation
    if (!m_renderParams.needColorSegmentation()) {
      dewarped = QImage();
    }

    BinaryImage dewarpedBwContent = binarize(dewarpedAndMaybeSmoothed, dewarpingContentAreaMask);
    dewarpedAndMaybeSmoothed = QImage();  // Save memory.
    if (m_dbg) {
      m_dbg->add(dewarpedBwContent, "dewarpedBwContent");
    }
    m_status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(dewarpedBwContent);
      m_status.throwIfCancelled();
    }

    // It's important to keep despeckling the very last operation
    // affecting the binary part of the output. That's because
    // we will be reconstructing the input to this despeckling
    // operation from the final output file.
    maybeDespeckleInPlace(dewarpedBwContent, m_outRect, m_outRect, m_despeckleLevel, specklesImage, m_dpi);

    if (!m_renderParams.needColorSegmentation()) {
      if (!m_blackOnWhite) {
        dewarpedBwContent.invert();
      }

      detail::applyFillZonesInPlace(dewarpedBwContent, fillZones, origToOutput);

      imageBuilder.setImage(dewarpedBwContent.toQImage());
    } else {
      QImage segmentedImage = colorText(dewarpedBwContent, dewarped);
      dewarped = QImage();
      dewarpedBwContent.release();


      if (!m_blackOnWhite) {
        segmentedImage.invertPixels();
      }

      detail::applyFillZonesInPlace(segmentedImage, fillZones, origToOutput, false);
      if (m_dbg) {
        m_dbg->add(segmentedImage, "segmented_with_fill_zones");
      }
      m_status.throwIfCancelled();

      imageBuilder.setImage(segmentedImage);
    }
    return imageBuilder.build();
  }

  BinaryImage dewarpedBwMask;
  BinaryImage dewarpedBwContent;
  if (m_renderParams.mixedOutput()) {
    const QTransform origToWorkingCs
        = m_xform.transform() * QTransform().translate(-m_workingBoundingRect.left(), -m_workingBoundingRect.top());
    QTransform workingToOutputCs = QTransform().translate(m_workingBoundingRect.left(), m_workingBoundingRect.top());
    dewarpedBwMask = BinaryImage(dewarp(origToWorkingCs, warpedBwMask.toQImage(), workingToOutputCs, distortionModel,
                                        depthPerception, Qt::black));
    warpedBwMask.release();
    detail::applyAffineTransform(dewarpedBwMask, rotateXform, BLACK);
    detail::fillMarginsInPlace(dewarpedBwMask, dewarpingContentAreaMask, BLACK);
    if (m_dbg) {
      m_dbg->add(dewarpedBwMask, "dewarpedBwMask");
    }
    m_status.throwIfCancelled();

    if (m_renderParams.needBinarization()) {
      QImage dewarpedAndMaybeSmoothed;
      if (!m_renderParams.needSavitzkyGolaySmoothing()) {
        dewarpedAndMaybeSmoothed = dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming());
      } else {
        dewarpedAndMaybeSmoothed = detail::smoothToGrayscale(
            dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming()), m_dpi);
        if (m_dbg) {
          m_dbg->add(dewarpedAndMaybeSmoothed, "smoothed");
        }
        m_status.throwIfCancelled();
      }

      BinaryImage dewarpedBwMaskFilled(dewarpedBwMask);
      detail::fillMarginsInPlace(dewarpedBwMaskFilled, dewarpingContentAreaMask, WHITE);
      dewarpedBwContent = binarize(dewarpedAndMaybeSmoothed, dewarpedBwMaskFilled);
      dewarpedBwMaskFilled.release();
      dewarpedAndMaybeSmoothed = QImage();  // Save memory.
      if (m_dbg) {
        m_dbg->add(dewarpedBwContent, "dewarpedBwContent");
      }
      m_status.throwIfCancelled();

      if (m_renderParams.needMorphologicalSmoothing()) {
        morphologicalSmoothInPlace(dewarpedBwContent);
      }

      // It's important to keep despeckling the very last operation
      // affecting the binary part of the output. That's because
      // we will be reconstructing the input to this despeckling
      // operation from the final output file.
      maybeDespeckleInPlace(dewarpedBwContent, m_outRect, m_croppedContentRect, m_despeckleLevel, specklesImage, m_dpi);

      if (m_renderParams.originalBackground()) {
        // See processWithoutDewarping(): keep the binarized content out of picture areas.
        rasterOp<RopAnd<RopSrc, RopDst>>(dewarpedBwContent, dewarpedBwMask);
      }

      if (!m_renderParams.normalizeIlluminationColor()) {
        m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
            m_colorOriginal ? m_inputOrigImage : m_inputGrayImage, m_outCropAreaInOriginalCs);

        {
          QImage origWithoutIllumination;
          if (m_colorOriginal) {
            origWithoutIllumination = m_inputOrigImage;
          } else {
            origWithoutIllumination = m_inputGrayImage;
          }
          dewarped = dewarp(QTransform(), origWithoutIllumination, m_xform.transform(), distortionModel,
                            depthPerception, m_outsideBackgroundColor);
        }
        detail::applyAffineTransform(dewarped, rotateXform, m_outsideBackgroundColor);
        applyEnhancementFilters(dewarped);
        m_status.throwIfCancelled();
      }

      if (!m_renderParams.needColorSegmentation()) {
        if (m_renderParams.originalBackground()) {
          combineImages(dewarped, dewarpedBwContent);
        } else {
          combineImages(dewarped, dewarpedBwContent, dewarpedBwMask);
        }
      } else {
        QImage segmentedImage = colorText(dewarpedBwContent, dewarped);

        if (m_renderParams.originalBackground()) {
          combineImages(dewarped, segmentedImage);
        } else {
          combineImages(dewarped, segmentedImage, dewarpedBwMask);
        }
      }

      if (m_dbg) {
        m_dbg->add(dewarped, "combined");
      }

      if (m_renderParams.originalBackground()) {
        detail::reserveBlackAndWhite(dewarped, dewarpedBwContent.inverted());
      } else {
        detail::reserveBlackAndWhite(dewarped, dewarpedBwMask.inverted());
      }
      m_status.throwIfCancelled();
    }
  }

  applyPhotoAdjustments(dewarped, m_renderParams.mixedOutput() ? &dewarpedBwMask : nullptr);

  // The same filling colors as without dewarping; "Black" was missing here.
  if (m_renderParams.needBinarization() && !m_renderParams.originalBackground()) {
    // The image is inverted back below for light content on dark background.
    switch (m_colorParams.colorCommonOptions().getFillingColor()) {
      case FILL_WHITE:
        m_outsideBackgroundColor = m_blackOnWhite ? Qt::white : Qt::black;
        break;
      case FILL_BLACK:
        m_outsideBackgroundColor = m_blackOnWhite ? Qt::black : Qt::white;
        break;
      default:
        m_outsideBackgroundColor = Qt::white;
    }
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_WHITE) {
    m_outsideBackgroundColor = m_blackOnWhite ? Qt::white : Qt::black;
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_BLACK) {
    m_outsideBackgroundColor = m_blackOnWhite ? Qt::black : Qt::white;
  }
  detail::fillMarginsInPlace(dewarped, dewarpingContentAreaMask, m_outsideBackgroundColor);

  if (!m_blackOnWhite) {
    dewarped.invertPixels();
  }

  // applyFillZonesToMixedInPlace() re-binarizes the non-picture areas, which would
  // wipe out the original background, so paint fill zones directly in that mode.
  if (m_renderParams.mixedOutput() && m_renderParams.needBinarization() && !m_renderParams.originalBackground()) {
    detail::applyFillZonesToMixedInPlace(dewarped, fillZones, origToOutput, dewarpedBwMask,
                                         !m_renderParams.needColorSegmentation());
  } else {
    detail::applyFillZonesInPlace(dewarped, fillZones, origToOutput);
  }
  if (m_renderParams.originalBackground()) {
    detail::applyFillZonesToMask(dewarpedBwContent, fillZones, origToOutput);
    rasterOp<RopAnd<RopSrc, RopDst>>(dewarpedBwContent, dewarpedBwMask);
  }
  if (m_dbg) {
    m_dbg->add(dewarped, "fillZones");
  }
  m_status.throwIfCancelled();

  if (m_renderParams.posterize() && !m_renderParams.needBinarization()) {
    dewarped = posterizeImage(dewarped);
  }

  if (m_renderParams.splitOutput()) {
    imageBuilder.setForegroundType(getForegroundType());
    if (!m_renderParams.originalBackground()) {
      imageBuilder.setForegroundMask(dewarpedBwMask);
    } else {
      imageBuilder.setForegroundMask(dewarpedBwContent);
      imageBuilder.setBackgroundMask(dewarpedBwMask.inverted());
    }
  }
  return imageBuilder.setImage(dewarped).build();
}

GrayImage detail::Processor::normalizeIlluminationGray(const QImage& input,
                                                       const QPolygonF& areaToConsider,
                                                       const QTransform& xform,
                                                       const QRect& targetRect,
                                                       GrayImage* background) const {
  GrayImage toBeNormalized = transformToGray(input, xform, targetRect, OutsidePixels::assumeWeakNearest());
  if (m_dbg) {
    m_dbg->add(toBeNormalized, "toBeNormalized");
  }

  m_status.throwIfCancelled();

  QPolygonF transformedConsiderationArea = xform.map(areaToConsider);
  transformedConsiderationArea.translate(-targetRect.topLeft());

  const PolynomialSurface bgPs = estimateBackground(toBeNormalized, transformedConsiderationArea, m_status, m_dbg);
  m_status.throwIfCancelled();

  GrayImage bgImg(bgPs.render(toBeNormalized.size()));
  if (m_dbg) {
    m_dbg->add(bgImg, "background");
  }
  if (background) {
    *background = bgImg;
  }
  m_status.throwIfCancelled();

  grayRasterOp<detail::RaiseAboveBackground>(bgImg, toBeNormalized);
  if (m_dbg) {
    m_dbg->add(bgImg, "normalized_illumination");
  }
  m_status.throwIfCancelled();
  return bgImg;
}

BinaryImage detail::Processor::estimateBinarizationMask(const GrayImage& graySource, const QRect& sourceRect) const {
  const QSize downscaledSize(detail::to300dpi(graySource.size(), m_dpi));

  // A 300dpi version of graySource.
  GrayImage downscaledInput(scaleToGray(graySource, downscaledSize));
  m_status.throwIfCancelled();

  // Light areas indicate pictures.
  GrayImage pictureAreas(detectPictures(downscaledInput));
  downscaledInput = GrayImage();  // Save memory.
  m_status.throwIfCancelled();

  const BinaryThreshold threshold(48);
  // Scale back to original size.
  pictureAreas = scaleToGray(pictureAreas, sourceRect.size());
  return BinaryImage(pictureAreas, threshold);
}

void detail::Processor::modifyBinarizationMask(BinaryImage& bwMask,
                                               BinaryImage& bwContent,
                                               const QRect& maskRect,
                                               const ZoneSet& zones) const {
  QTransform xform = m_xform.transform();
  xform *= QTransform().translate(-maskRect.x(), -maskRect.y());
  BinaryImage bwContentBG(bwContent);

  using PLP = PictureLayerProperty;

  // Pass 1: ZONEERASER1
  for (const Zone& zone : zones) {
    if (zone.properties().locateOrDefault<PLP>()->layer() == PLP::ZONEERASER1) {
      const QPolygonF poly(zone.spline().toPolygon());
      PolygonRasterizer::fill(bwMask, BLACK, xform.map(poly), Qt::WindingFill);
    }
  }

  // Pass 2: ZONEFG
  detail::BinaryImageXOR(bwMask, bwContent, WHITE);
  for (const Zone& zone : zones) {
    if (zone.properties().locateOrDefault<PLP>()->layer() == PLP::ZONEFG) {
      const QPolygonF poly(zone.spline().toPolygon());
      PolygonRasterizer::fill(bwMask, WHITE, xform.map(poly), Qt::WindingFill);
      PolygonRasterizer::fill(bwContentBG, WHITE, xform.map(poly), Qt::WindingFill);
    }
  }
  detail::BinaryImageXOR(bwMask, bwContent, WHITE);

  // Pass 3: ZONEBG
  detail::BinaryImageXOR(bwMask, bwContentBG, BLACK);
  for (const Zone& zone : zones) {
    if (zone.properties().locateOrDefault<PLP>()->layer() == PLP::ZONEBG) {
      const QPolygonF poly(zone.spline().toPolygon());
      PolygonRasterizer::fill(bwMask, WHITE, xform.map(poly), Qt::WindingFill);
    }
  }
  detail::BinaryImageXOR(bwMask, bwContentBG, BLACK);

  // Pass 4: ZONEPAINTER2
  for (const Zone& zone : zones) {
    if (zone.properties().locateOrDefault<PLP>()->layer() == PLP::ZONEPAINTER2) {
      const QPolygonF poly(zone.spline().toPolygon());
      PolygonRasterizer::fill(bwMask, WHITE, xform.map(poly), Qt::WindingFill);
    }
  }

  // Pass 5: ZONEERASER3
  for (const Zone& zone : zones) {
    if (zone.properties().locateOrDefault<PLP>()->layer() == PLP::ZONEERASER3) {
      const QPolygonF poly(zone.spline().toPolygon());
      PolygonRasterizer::fill(bwMask, BLACK, xform.map(poly), Qt::WindingFill);
    }
  }

  detail::BinaryImageXOR(bwContent, bwMask, WHITE);
}


/**
 * Set up a distortion model corresponding to the content rect,
 * which will result in no distortion correction.
 */
void detail::Processor::setupTrivialDistortionModel(DistortionModel& distortionModel) const {
  QPolygonF poly;
  if (!m_contentRect.isEmpty()) {
    poly = QRectF(m_contentRect);
  } else {
    poly << m_contentRect.topLeft() + QPointF(-0.5, -0.5);
    poly << m_contentRect.topLeft() + QPointF(0.5, -0.5);
    poly << m_contentRect.topLeft() + QPointF(0.5, 0.5);
    poly << m_contentRect.topLeft() + QPointF(-0.5, 0.5);
  }
  poly = m_xform.transformBack().map(poly);

  std::vector<QPointF> topPolyline, bottomPolyline;
  topPolyline.push_back(poly[0]);     // top-left
  topPolyline.push_back(poly[1]);     // top-right
  bottomPolyline.push_back(poly[3]);  // bottom-left
  bottomPolyline.push_back(poly[2]);  // bottom-right
  distortionModel.setTopCurve(Curve(topPolyline));
  distortionModel.setBottomCurve(Curve(bottomPolyline));
}

CylindricalSurfaceDewarper detail::Processor::createDewarper(const DistortionModel& distortionModel,
                                                             const QTransform& distortionModelToTarget,
                                                             double depthPerception) {
  if (distortionModelToTarget.isIdentity()) {
    return CylindricalSurfaceDewarper(distortionModel.topCurve().polyline(), distortionModel.bottomCurve().polyline(),
                                      depthPerception, distortionModel.widthCorrection());
  }

  std::vector<QPointF> topPolyline(distortionModel.topCurve().polyline());
  std::vector<QPointF> bottomPolyline(distortionModel.bottomCurve().polyline());
  for (QPointF& pt : topPolyline) {
    pt = distortionModelToTarget.map(pt);
  }
  for (QPointF& pt : bottomPolyline) {
    pt = distortionModelToTarget.map(pt);
  }
  return CylindricalSurfaceDewarper(topPolyline, bottomPolyline, depthPerception, distortionModel.widthCorrection());
}

/**
 * \param origToSrc Transformation from the original image coordinates
 *                    to the coordinate system of \p src image.
 * \param srcToOutput Transformation from the \p src image coordinates
 *                      to output image coordinates.
 * \param distortionModel Distortion model.
 * \param depthPerception Depth perception.
 * \param bgColor The color to use for areas outside of \p src.
 */
QImage detail::Processor::dewarp(const QTransform& origToSrc,
                                 const QImage& src,
                                 const QTransform& srcToOutput,
                                 const DistortionModel& distortionModel,
                                 const DepthPerception& depthPerception,
                                 const QColor& bgColor) const {
  const CylindricalSurfaceDewarper dewarper(createDewarper(distortionModel, origToSrc, depthPerception.value()));

  // Model domain is a rectangle in output image coordinates that
  // will be mapped to our curved quadrilateral.
  const QRect modelDomain(distortionModel.modelDomain(dewarper, origToSrc * srcToOutput, m_outRect).toRect());
  if (modelDomain.isEmpty()) {
    // Same size as on the normal path, or the results don't match each other and the masks.
    GrayImage out(m_outRect.size());
    out.fill(0xff);  // white
    return out;
  }
  return RasterDewarper::dewarp(src, m_outRect.size(), dewarper, modelDomain, bgColor);
}

void detail::Processor::evenOutLetterWidths(DistortionModel& distortionModel,
                                            const DepthPerception& depthPerception,
                                            const QImage& src,
                                            QImage& dewarped) const {
  const std::vector<WidthProfile::Sample> samples
      = WidthProfile::measure(GrayImage(dewarped), m_croppedContentRect, m_dpi.horizontal());
  // The same model domain as in dewarp(), where output x maps to (x - left) / width, as in RasterDewarper.
  const CylindricalSurfaceDewarper dewarper(createDewarper(distortionModel, QTransform(), depthPerception.value()));
  const QRect modelDomain(distortionModel.modelDomain(dewarper, m_xform.transform(), m_outRect).toRect());
  const WidthCorrection correction = WidthProfile::toCorrection(samples, modelDomain.left(), modelDomain.width());
  if (correction.isNull()) {
    return;
  }
  m_status.throwIfCancelled();

  distortionModel.setWidthCorrection(correction);
  dewarped = dewarp(QTransform(), src, m_xform.transform(), distortionModel, depthPerception, m_outsideBackgroundColor);
}

GrayImage detail::Processor::detectPictures(const GrayImage& input300dpi) const {
  return ::detectPictures(input300dpi, m_pictureShapeOptions.isHigherSearchSensitivity(), m_status, m_dbg);
}

BinaryThreshold detail::Processor::adjustThreshold(BinaryThreshold threshold) const {
  return adjustedThreshold(threshold, m_colorParams.blackWhiteOptions());
}

BinaryThreshold detail::Processor::calcBinarizationThreshold(const QImage& image, const BinaryImage& mask) const {
  GrayscaleHistogram hist(image, mask);
  return adjustThreshold(BinaryThreshold::otsuThreshold(hist));
}

BinaryThreshold detail::Processor::calcBinarizationThreshold(const QImage& image,
                                                             const QPolygonF& cropArea,
                                                             const BinaryImage* mask) const {
  QPainterPath path;
  path.addPolygon(cropArea);

  if (path.contains(image.rect())) {
    return adjustThreshold(BinaryThreshold::otsuThreshold(image));
  } else {
    BinaryImage modifiedMask(image.size(), BLACK);
    PolygonRasterizer::fillExcept(modifiedMask, WHITE, cropArea, Qt::WindingFill);
    modifiedMask = erodeBrick(modifiedMask, QSize(3, 3), WHITE);

    if (mask) {
      rasterOp<RopAnd<RopSrc, RopDst>>(modifiedMask, *mask);
    }
    return calcBinarizationThreshold(image, modifiedMask);
  }
}

void detail::Processor::morphologicalSmoothInPlace(BinaryImage& binImg) const {
  // When removing black noise, remove small ones first.

  {
    const char pattern[]
        = "XXX"
          " - "
          "   ";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 3);
  }

  m_status.throwIfCancelled();

  {
    const char pattern[]
        = "X ?"
          "X  "
          "X- "
          "X- "
          "X  "
          "X ?";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 6);
  }

  m_status.throwIfCancelled();

  {
    const char pattern[]
        = "X ?"
          "X ?"
          "X  "
          "X- "
          "X- "
          "X- "
          "X  "
          "X ?"
          "X ?";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 9);
  }

  m_status.throwIfCancelled();

  {
    const char pattern[]
        = "XX?"
          "XX?"
          "XX "
          "X+ "
          "X+ "
          "X+ "
          "XX "
          "XX?"
          "XX?";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 9);
  }

  m_status.throwIfCancelled();

  {
    const char pattern[]
        = "XX?"
          "XX "
          "X+ "
          "X+ "
          "XX "
          "XX?";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 6);
  }

  m_status.throwIfCancelled();

  {
    const char pattern[]
        = "   "
          "X+X"
          "XXX";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 3);
  }

  if (m_dbg) {
    m_dbg->add(binImg, "edges_smoothed");
  }
}

BinaryImage detail::Processor::binarize(const QImage& image) const {
  return binarizeBlackWhite(image, m_colorParams.blackWhiteOptions());
}

BinaryImage detail::Processor::binarize(const QImage& image, const BinaryImage& mask) const {
  BinaryImage binarized = binarize(image);

  rasterOp<RopAnd<RopSrc, RopDst>>(binarized, mask);
  return binarized;
}

BinaryImage detail::Processor::binarize(const QImage& image, const QPolygonF& cropArea, const BinaryImage* mask) const {
  QPainterPath path;
  path.addPolygon(cropArea);

  if (path.contains(image.rect()) && !mask) {
    return binarize(image);
  } else {
    BinaryImage modifiedMask(image.size(), BLACK);
    PolygonRasterizer::fillExcept(modifiedMask, WHITE, cropArea, Qt::WindingFill);
    modifiedMask = erodeBrick(modifiedMask, QSize(3, 3), WHITE);

    if (mask) {
      rasterOp<RopAnd<RopSrc, RopDst>>(modifiedMask, *mask);
    }
    return binarize(image, modifiedMask);
  }
}

/**
 * \brief Remove small connected components that are considered to be garbage.
 *
 * Both the size and the distance to other components are taken into account.
 *
 * \param[in,out] image The image to despeckle.
 * \param imageRect The rectangle corresponding to \p image in the same
 *        coordinate system where m_contentRect and m_cropRect are defined.
 * \param maskRect The area within the image to consider. Defined not
 *        relative to \p image, but in the same coordinate system where
 *        m_contentRect and m_cropRect are defined. This only affects
 *        \p specklesImg, if provided.
 * \param level Despeckling aggressiveness.
 * \param specklesImg If provided, the removed black speckles will be written
 *        there. The speckles image is always considered to correspond
 *        to m_cropRect, so it will have the size of m_cropRect.size().
 *        Only the area within \p maskRect will be copied to \p specklesImg.
 *        The rest will be filled with white.
 * \param dpi The DPI of the input image. See the note below.
 * \param status Task status.
 * \param dbg An optional sink for debugging images.
 *
 * \note This function only works effectively when the DPI is symmetric,
 * that is, its horizontal and vertical components are equal.
 */
void detail::Processor::maybeDespeckleInPlace(BinaryImage& image,
                                              const QRect& imageRect,
                                              const QRect& maskRect,
                                              double level,
                                              BinaryImage* specklesImg,
                                              const Dpi& dpi) const {
  const QRect srcRect(maskRect.translated(-imageRect.topLeft()));
  const QRect dstRect(maskRect);

  if (specklesImg) {
    BinaryImage(m_outRect.size(), WHITE).swap(*specklesImg);
    if (!maskRect.isEmpty()) {
      rasterOp<RopSrc>(*specklesImg, dstRect, image, srcRect.topLeft());
    }
  }

  if (level != 0) {
    Despeckle::despeckleInPlace(image, dpi, level, m_status, m_dbg);

    if (m_dbg) {
      m_dbg->add(image, "despeckled");
    }
  }

  if (specklesImg) {
    if (!maskRect.isEmpty()) {
      rasterOp<RopSubtract<RopDst, RopSrc>>(*specklesImg, dstRect, image, srcRect.topLeft());
    }
  }

  m_status.throwIfCancelled();
}

double detail::Processor::findSkew(const QImage& image) const {
  if (m_dewarpingOptions.needPostDeskew()
      && ((m_dewarpingOptions.dewarpingMode() == MARGINAL) || (m_dewarpingOptions.dewarpingMode() == MANUAL))) {
    const BinaryImage bwImage(image, BinaryThreshold::otsuThreshold(GrayscaleHistogram(image)));
    const Skew skew = SkewFinder().findSkew(bwImage);
    if ((skew.angle() != .0) && (skew.confidence() >= Skew::GOOD_CONFIDENCE)) {
      return skew.angle();
    }
  }
  return .0;
}

QImage detail::Processor::segmentImage(const BinaryImage& image, const QImage& colorImage) const {
  const BlackWhiteOptions::ColorSegmenterOptions& segmenterOptions
      = m_colorParams.blackWhiteOptions().getColorSegmenterOptions();
  ColorSegmenter segmenter(m_dpi, segmenterOptions.getNoiseReduction(), segmenterOptions.getRedThresholdAdjustment(),
                           segmenterOptions.getGreenThresholdAdjustment(),
                           segmenterOptions.getBlueThresholdAdjustment());

  QImage segmented;
  {
    if (m_colorOriginal) {
      segmented = segmenter.segment(image, colorImage);
    } else {
      segmented = segmenter.segment(image, GrayImage(colorImage));
    }
  }

  if (m_dbg) {
    m_dbg->add(segmented, "segmented");
  }
  m_status.throwIfCancelled();
  return segmented;
}

QImage detail::Processor::posterizeImage(const QImage& image, const QColor& backgroundColor) const {
  const ColorCommonOptions::PosterizationOptions& posterizationOptions
      = m_colorParams.colorCommonOptions().getPosterizationOptions();
  Posterizer posterizer(posterizationOptions.getLevel(), posterizationOptions.isNormalizationEnabled(),
                        posterizationOptions.isForceBlackAndWhite(), 0, qRound(backgroundColor.lightnessF() * 255));

  QImage posterized = posterizer.posterize(image);

  if (m_dbg) {
    m_dbg->add(posterized, "posterized");
  }
  m_status.throwIfCancelled();
  return posterized;
}

QImage detail::Processor::colorText(const BinaryImage& text, const QImage& colorImage) const {
  const BlackWhiteOptions::ColorSegmenterOptions& options
      = m_colorParams.blackWhiteOptions().getColorSegmenterOptions();
  if (options.isKMeansEnabled()) {
    // Letters up to a filled square of a quarter inch get one color for each of their segments, so
    // the red, green and blue thresholds and the noise reduction apply to them; larger areas keep
    // their shading. An indexed image, as after posterizing, keeps the files small.
    const ConnectivityMap segments
        = m_colorOriginal ? ColorSegmenter(m_dpi, options.getNoiseReduction(), options.getRedThresholdAdjustment(),
                                           options.getGreenThresholdAdjustment(), options.getBlueThresholdAdjustment())
                                .segmentMap(text, colorImage)
                          : ConnectivityMap(text, CONN8);
    m_status.throwIfCancelled();
    const int quarterInch = (m_dpi.horizontal() + m_dpi.vertical()) / 8;
    QImage colored
        = Posterizer::convertToIndexed(reduceTextColorsKMeans(colorImage, text, segments, options.getKMeansColors(),
                                                              options.isKMeansBlackFixed(), quarterInch * quarterInch));
    if (m_dbg) {
      m_dbg->add(colored, "k-means");
    }
    m_status.throwIfCancelled();
    return colored;
  }
  QImage segmented = segmentImage(text, colorImage);
  if (m_renderParams.posterize()) {
    segmented = posterizeImage(segmented, m_outsideBackgroundColor);
  }
  return segmented;
}

void detail::Processor::processPictureZones(BinaryImage& mask, ZoneSet& pictureZones, const GrayImage& image) {
  if ((m_pictureShapeOptions.getPictureShape() != RECTANGULAR_SHAPE) || !m_outputProcessingParams.isAutoZonesFound()) {
    if (m_pictureShapeOptions.getPictureShape() != OFF_SHAPE) {
      mask = estimateBinarizationMask(image, m_workingBoundingRect);
    }

    detail::removeAutoPictureZones(pictureZones);
    m_settings->setPictureZones(m_pageId, pictureZones);
    m_outputProcessingParams.setAutoZonesFound(false);
    m_settings->setOutputProcessingParams(m_pageId, m_outputProcessingParams);
  }
  if ((m_pictureShapeOptions.getPictureShape() == RECTANGULAR_SHAPE) && !m_outputProcessingParams.isAutoZonesFound()) {
    std::vector<QRect> areas = detail::findRectAreas(mask, m_pictureShapeOptions.getSensitivity());

    const QTransform fromWorkingCs = [this]() {
      QTransform toWorkingCs = m_xform.transform();
      toWorkingCs *= QTransform().translate(-m_workingBoundingRect.x(), -m_workingBoundingRect.y());
      return toWorkingCs.inverted();
    }();

    for (const QRect& area : areas) {
      pictureZones.add(detail::createPictureZoneFromPoly(fromWorkingCs.map(QRectF(area))));
    }
    m_settings->setPictureZones(m_pageId, pictureZones);
    m_outputProcessingParams.setAutoZonesFound(true);
    m_settings->setOutputProcessingParams(m_pageId, m_outputProcessingParams);

    mask.fill(BLACK);
  }
}

ForegroundType detail::Processor::getForegroundType() const {
  if (m_renderParams.needBinarization()) {
    if (m_renderParams.needColorSegmentation()) {
      return ForegroundType::INDEXED;
    } else {
      return ForegroundType::BINARY;
    }
  } else {
    return ForegroundType::COLOR;
  }
}

void detail::Processor::applyEnhancementFilters(QImage& image) const {
  const ColorCommonOptions& colorCommonOptions = m_colorParams.colorCommonOptions();
  if (colorCommonOptions.chromaDenoise()) {
    chromaDenoiseInPlace(image, colorCommonOptions.chromaRadius(), colorCommonOptions.chromaGrayLimit());
  }
  subtractBackgroundColorInPlace(image, colorCommonOptions.subtractBackgroundRadius(),
                                 colorCommonOptions.subtractBackgroundCoef(),
                                 colorCommonOptions.subtractBackgroundWhite());
  knnDenoiseColorInPlace(image, colorCommonOptions.knnRadius(), colorCommonOptions.knnCoef());
  wienerColorFilterInPlace(image, QSize(colorCommonOptions.wienerWindowSize(), colorCommonOptions.wienerWindowSize()),
                           colorCommonOptions.wienerCoef());
  sharpenColorInPlace(image, colorCommonOptions.sharpenRadius(), colorCommonOptions.sharpenCoef());
}

void detail::Processor::applyPhotoAdjustments(QImage& image, const BinaryImage* pictureMask) const {
  const PhotoAdjustments& adjustments = m_colorParams.photoAdjustments();
  if (adjustments.isNeutral()) {
    return;
  }
  // Pages with white text on black are processed inverted.
  adjustments.applyInPlace(image, pictureMask, !m_blackOnWhite);
  if (pictureMask && m_renderParams.needBinarization()) {
    // Pure black and white mark the text of mixed output; the adjusted pictures must not use them.
    detail::reserveBlackAndWhite(image, pictureMask->inverted());
  }
  m_status.throwIfCancelled();
}

QImage detail::Processor::transformToWorkingCs(bool normalize) const {
  QImage dst;
  if (normalize) {
    dst = normalizeIlluminationGray(m_inputGrayImage, m_preCropAreaInOriginalCs, m_xform.transform(),
                                    m_workingBoundingRect);
    if (m_colorOriginal) {
      assert(dst.format() == QImage::Format_Indexed8);
      QImage colorImg = transform(m_inputOrigImage, m_xform.transform(), m_workingBoundingRect,
                                  OutsidePixels::assumeColor(m_outsideBackgroundColor));
      adjustBrightnessGrayscale(colorImg, dst);
      dst = colorImg;
    }
  } else {
    if (!m_colorOriginal) {
      dst = transformToGray(m_inputGrayImage, m_xform.transform(), m_workingBoundingRect,
                            OutsidePixels::assumeColor(m_outsideBackgroundColor));
    } else {
      dst = transform(m_inputOrigImage, m_xform.transform(), m_workingBoundingRect,
                      OutsidePixels::assumeColor(m_outsideBackgroundColor));
    }
  }
  m_status.throwIfCancelled();
  return dst;
}

DistortionModel detail::Processor::buildTrapezoidDistortionModel(const GrayImage& warpedGrayOutput,
                                                                 const QTransform& toOriginal) {
  return straightModelInOriginal(
      TextLineTracer::traceTrapezoid(warpedGrayOutput, m_dpi, m_contentBoxInTracingCs, m_dbg), toOriginal);
}

DistortionModel detail::Processor::straightModelInOriginal(DistortionModel distortionModel,
                                                           const QTransform& toOriginal) {
  if (!distortionModel.isValid()) {
    m_dewarpingOptions.setNoModelFound(true);
    setupTrivialDistortionModel(distortionModel);
    return distortionModel;
  }

  const auto mappedToOriginal = [&toOriginal](const Curve& curve) {
    std::vector<QPointF> polyline(curve.polyline());
    for (QPointF& pt : polyline) {
      pt = toOriginal.map(pt);
    }
    return Curve(polyline);
  };
  distortionModel.setTopCurve(mappedToOriginal(distortionModel.topCurve()));
  distortionModel.setBottomCurve(mappedToOriginal(distortionModel.bottomCurve()));
  return distortionModel;
}

DistortionModel detail::Processor::buildAutoDistortionModel(const GrayImage& warpedGrayOutput,
                                                            const QTransform& toOriginal) {
  DistortionModelBuilder modelBuilder(Vec2d(0, 1));

  // A flat page gets a model of straight lines: single traced lines are too inexact to be its curves.
  const std::optional<DistortionModel> flatPageModel
      = TextLineTracer::trace(warpedGrayOutput, m_dpi, m_contentBoxInTracingCs, modelBuilder, m_status, m_dbg);
  if (flatPageModel) {
    return straightModelInOriginal(*flatPageModel, toOriginal);
  }
  modelBuilder.transform(toOriginal);

  TopBottomEdgeTracer::trace(m_inputGrayImage, modelBuilder.verticalBounds(), modelBuilder, m_status, m_dbg);

  DistortionModel distortionModel = modelBuilder.tryBuildModel(m_dbg, &m_inputGrayImage.toQImage());
  if (!distortionModel.isValid()) {
    m_dewarpingOptions.setNoModelFound(true);
    setupTrivialDistortionModel(distortionModel);
  }
  return distortionModel;
}

DistortionModel detail::Processor::buildMarginalDistortionModel() const {
  BinaryImage bwImage(m_inputGrayImage, BinaryThreshold(64));

  QTransform transform = m_xform.preRotation().transform(bwImage.size());
  QTransform invTransform = transform.inverted();

  int degrees = m_xform.preRotation().toDegrees();
  bwImage = orthogonalRotation(bwImage, degrees);

  DistortionModel distortionModel;
  setupTrivialDistortionModel(distortionModel);

  int maxRedPoints = 5;
  XSpline topSpline;

  const std::vector<QPointF>& topPolyline = distortionModel.topCurve().polyline();

  const QLineF topLine(transform.map(topPolyline.front()), transform.map(topPolyline.back()));

  topSpline.appendControlPoint(topLine.p1(), 0);

  if ((m_pageId.subPage() == PageId::SINGLE_PAGE) || (m_pageId.subPage() == PageId::LEFT_PAGE)) {
    for (int i = 29 - maxRedPoints; i < 29; i++) {
      topSpline.appendControlPoint(topLine.pointAt((float) i / 29.0), 1);
    }
  } else {
    for (int i = 1; i <= maxRedPoints; i++) {
      topSpline.appendControlPoint(topLine.pointAt((float) i / 29.0), 1);
    }
  }

  topSpline.appendControlPoint(topLine.p2(), 0);

  for (int i = 0; i <= topSpline.numSegments(); i++) {
    detail::movePointToTopMargin(bwImage, topSpline, i);
  }

  for (int i = 0; i <= topSpline.numSegments(); i++) {
    topSpline.moveControlPoint(i, invTransform.map(topSpline.controlPointPosition(i)));
  }

  distortionModel.setTopCurve(Curve(topSpline));


  XSpline bottomSpline;

  const std::vector<QPointF>& bottomPolyline = distortionModel.bottomCurve().polyline();

  const QLineF bottomLine(transform.map(bottomPolyline.front()), transform.map(bottomPolyline.back()));

  bottomSpline.appendControlPoint(bottomLine.p1(), 0);

  if ((m_pageId.subPage() == PageId::SINGLE_PAGE) || (m_pageId.subPage() == PageId::LEFT_PAGE)) {
    for (int i = 29 - maxRedPoints; i < 29; i++) {
      bottomSpline.appendControlPoint(bottomLine.pointAt((float) i / 29.0), 1);
    }
  } else {
    for (int i = 1; i <= maxRedPoints; i++) {
      bottomSpline.appendControlPoint(bottomLine.pointAt((float) i / 29.0), 1);
    }
  }

  bottomSpline.appendControlPoint(bottomLine.p2(), 0);

  for (int i = 0; i <= bottomSpline.numSegments(); i++) {
    detail::movePointToBottomMargin(bwImage, bottomSpline, i);
  }

  for (int i = 0; i <= bottomSpline.numSegments(); i++) {
    bottomSpline.moveControlPoint(i, invTransform.map(bottomSpline.controlPointPosition(i)));
  }

  distortionModel.setBottomCurve(Curve(bottomSpline));

  if (!distortionModel.isValid()) {
    setupTrivialDistortionModel(distortionModel);
  }

  // REVIEW-NOTE (2026-10): the control points are in original coordinates but drawn onto the pre-rotated
  // bwImage -> on rotated pages the debug image shows them at wrong places. Not fixed because it only
  // affects the debug output.
  if (m_dbg) {
    QImage outImage(bwImage.toQImage().convertToFormat(QImage::Format_RGB32));
    for (int i = 0; i <= topSpline.numSegments(); i++) {
      detail::drawPoint(outImage, topSpline.controlPointPosition(i));
    }
    for (int i = 0; i <= bottomSpline.numSegments(); i++) {
      detail::drawPoint(outImage, bottomSpline.controlPointPosition(i));
    }
    m_dbg->add(outImage, "marginal dewarping");
  }
  return distortionModel;
}
}  // namespace output
