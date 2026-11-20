// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGenerator.h"

#include <ColoredDimming.h>
#include <DrawOver.h>
#include <RasterOp.h>
#include <imageproc/BackgroundColorCalculator.h>
#include <imageproc/ImageCombination.h>

#include <QColor>
#include <QImage>
#include <new>

#include "DebugImages.h"
#include "OutputGeneratorProcessor.h"
#include "OutputImageBuilder.h"
#include "OutputImageOps.h"
#include "OutputZoneOps.h"
#include "Settings.h"
#include "TaskStatus.h"

using namespace imageproc;
namespace output {
std::unique_ptr<OutputImage> detail::Processor::processWithoutDewarping(ZoneSet& pictureZones,
                                                                        const ZoneSet& fillZones,
                                                                        BinaryImage* autoPictureMask,
                                                                        BinaryImage* specklesImage) {
  QImage maybeNormalized, maybeSmoothed;
  BinaryImage bwContent, bwContentMaskOutput, bwContentOutput;
  OutputImageBuilder imageBuilder;

  maybeNormalized = transformToWorkingCs(m_renderParams.normalizeIllumination());
  if (m_inputs.dbg) {
    m_inputs.dbg->add(maybeNormalized, "maybeNormalized");
  }

  applyEnhancementFilters(maybeNormalized);

  if (m_renderParams.normalizeIllumination()) {
    m_outsideBackgroundColor
        = BackgroundColorCalculator::calcDominantBackgroundColor(maybeNormalized, m_geometry.outCropAreaInWorkingCs);
  }

  m_inputs.status.throwIfCancelled();

  if (m_renderParams.binaryOutput() || m_renderParams.mixedOutput()) {
    // BW mask begin
    if (!m_renderParams.needSavitzkyGolaySmoothing()) {
      maybeSmoothed = dimColoredPixels(maybeNormalized, m_colorParams.blackWhiteOptions().getColoredDimming());
    } else {
      maybeSmoothed = detail::smoothToGrayscale(
          dimColoredPixels(maybeNormalized, m_colorParams.blackWhiteOptions().getColoredDimming()), m_dpi);
      if (m_inputs.dbg) {
        m_inputs.dbg->add(maybeSmoothed, "smoothed");
      }
      m_inputs.status.throwIfCancelled();
    }

    bwContent = binarize(maybeSmoothed, m_geometry.contentAreaInWorkingCs);
    maybeSmoothed = QImage();  // Save memory.
    if (m_inputs.dbg) {
      m_inputs.dbg->add(bwContent, "binarized_and_cropped");
    }
    m_inputs.status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(bwContent);
      m_inputs.status.throwIfCancelled();
    }
    // BW mask end
  }

  // (BW only / Color segment) begin
  if (m_renderParams.binaryOutput()) {
    // don't destroy as it's needed for color segmentation
    if (!m_renderParams.needColorSegmentation()) {
      maybeNormalized = QImage();
    }

    BinaryImage dst(m_geometry.targetSize, WHITE);
    rasterOp<RopSrc>(dst, m_geometry.croppedContentRect, bwContent, m_geometry.contentRectInWorkingCs.topLeft());
    bwContent.release();  // Save memory.

    // It's important to keep despeckling the very last operation
    // affecting the binary part of the output. That's because
    // we will be reconstructing the input to this despeckling
    // operation from the final output file.
    maybeDespeckleInPlace(dst, m_inputs.outRect, m_inputs.outRect, m_despeckleLevel, specklesImage, m_dpi);

    if (!m_renderParams.needColorSegmentation()) {
      if (!m_images.blackOnWhite) {
        dst.invert();
      }

      detail::applyFillZonesInPlace(dst, fillZones, m_inputs.xform.transform());

      imageBuilder.setImage(dst.toQImage());
    } else {
      QImage segmentedImage;
      {
        QImage colorImage(m_geometry.targetSize, maybeNormalized.format());
        if (maybeNormalized.format() == QImage::Format_Indexed8) {
          colorImage.setColorTable(maybeNormalized.colorTable());
        }
        colorImage.fill(Qt::white);
        drawOver(colorImage, m_geometry.croppedContentRect, maybeNormalized, m_geometry.contentRectInWorkingCs);
        maybeNormalized = QImage();

        segmentedImage = colorText(dst, colorImage);
        dst.release();
      }


      if (!m_images.blackOnWhite) {
        segmentedImage.invertPixels();
      }

      detail::applyFillZonesInPlace(segmentedImage, fillZones, m_inputs.xform.transform(), false);
      if (m_inputs.dbg) {
        m_inputs.dbg->add(segmentedImage, "segmented_with_fill_zones");
      }
      m_inputs.status.throwIfCancelled();

      imageBuilder.setImage(segmentedImage);
    }
    return imageBuilder.build();
  }
  // (BW only / Color segment) end

  // Mixed begin
  if (m_renderParams.mixedOutput()) {
    BinaryImage bwMask(m_geometry.workingBoundingRect.size(), BLACK);
    processPictureZones(bwMask, pictureZones, GrayImage(maybeNormalized));
    if (m_inputs.dbg) {
      m_inputs.dbg->add(bwMask, "bwMask");
    }
    m_inputs.status.throwIfCancelled();

    if (autoPictureMask) {
      if (autoPictureMask->size() != m_geometry.targetSize) {
        BinaryImage(m_geometry.targetSize).swap(*autoPictureMask);
      }
      autoPictureMask->fill(BLACK);
      rasterOp<RopSrc>(*autoPictureMask, m_geometry.croppedContentRect, bwMask,
                       m_geometry.contentRectInWorkingCs.topLeft());
    }
    m_inputs.status.throwIfCancelled();

    modifyBinarizationMask(bwMask, bwContent, m_geometry.workingBoundingRect, pictureZones);
    detail::fillMarginsInPlace(bwMask, m_geometry.contentAreaInWorkingCs, BLACK);
    if (m_inputs.dbg) {
      m_inputs.dbg->add(bwMask, "bwMask with zones");
    }
    m_inputs.status.throwIfCancelled();

    if (m_renderParams.needBinarization()) {
      // It's important to keep despeckling the very last operation
      // affecting the binary part of the output. That's because
      // we will be reconstructing the input to this despeckling
      // operation from the final output file.
      maybeDespeckleInPlace(bwContent, m_geometry.workingBoundingRect, m_geometry.croppedContentRect, m_despeckleLevel,
                            specklesImage, m_dpi);

      if (m_renderParams.originalBackground()) {
        // The original background path combines without a mask, so keep the
        // binarized content out of picture areas, or it would be painted over the pictures.
        rasterOp<RopAnd<RopSrc, RopDst>>(bwContent, bwMask);
      }

      if (!m_renderParams.normalizeIlluminationColor()) {
        m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
            m_images.colorOriginal ? m_images.inputOrigImage : m_images.inputGrayImage,
            m_geometry.outCropAreaInOriginalCs);
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
      if (m_inputs.dbg) {
        m_inputs.dbg->add(maybeNormalized, "combined");
      }

      if (m_renderParams.originalBackground()) {
        detail::reserveBlackAndWhite(maybeNormalized, bwContent.inverted());
      } else {
        detail::reserveBlackAndWhite(maybeNormalized, bwMask.inverted());
      }
      m_inputs.status.throwIfCancelled();

      if (m_renderParams.originalBackground()) {
        bwContentOutput = BinaryImage(m_geometry.targetSize, WHITE);
        rasterOp<RopSrc>(bwContentOutput, m_geometry.croppedContentRect, bwContent,
                         m_geometry.contentRectInWorkingCs.topLeft());
      }
      // Release only now: the original background branches above still read bwContent.
      bwContent.release();  // Save memory.
    }

    applyPhotoAdjustments(maybeNormalized, &bwMask);

    bwContentMaskOutput = BinaryImage(m_geometry.targetSize, BLACK);
    rasterOp<RopSrc>(bwContentMaskOutput, m_geometry.croppedContentRect, bwMask,
                     m_geometry.contentRectInWorkingCs.topLeft());
  }
  // Mixed end

  if (!m_renderParams.mixedOutput()) {
    applyPhotoAdjustments(maybeNormalized, nullptr);
  }

  assert(!m_geometry.targetSize.isEmpty());
  QImage dst(m_geometry.targetSize, maybeNormalized.format());
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
        m_outsideBackgroundColor = m_images.blackOnWhite ? Qt::white : Qt::black;
        break;
      case FILL_BLACK:
        m_outsideBackgroundColor = m_images.blackOnWhite ? Qt::black : Qt::white;
        break;
      default:
        m_outsideBackgroundColor = Qt::white;
    }
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_WHITE) {
    m_outsideBackgroundColor = m_images.blackOnWhite ? Qt::white : Qt::black;
  } else if (m_colorParams.colorCommonOptions().getFillingColor() == FILL_BLACK) {
    m_outsideBackgroundColor = m_images.blackOnWhite ? Qt::black : Qt::white;
  }

  detail::fillMarginsInPlace(maybeNormalized, m_geometry.contentAreaInWorkingCs, m_outsideBackgroundColor);
  dst.fill(m_outsideBackgroundColor);

  drawOver(dst, m_geometry.croppedContentRect, maybeNormalized, m_geometry.contentRectInWorkingCs);
  maybeNormalized = QImage();

  if (!m_images.blackOnWhite) {
    dst.invertPixels();
  }

  // applyFillZonesToMixedInPlace() re-binarizes the non-picture areas, which would
  // wipe out the original background, so paint fill zones directly in that mode.
  if (m_renderParams.mixedOutput() && m_renderParams.needBinarization() && !m_renderParams.originalBackground()) {
    detail::applyFillZonesToMixedInPlace(dst, fillZones, m_inputs.xform.transform(), bwContentMaskOutput,
                                         !m_renderParams.needColorSegmentation());
  } else {
    detail::applyFillZonesInPlace(dst, fillZones, m_inputs.xform.transform());
  }
  if (m_renderParams.originalBackground()) {
    detail::applyFillZonesToMask(bwContentOutput, fillZones, m_inputs.xform.transform());
    rasterOp<RopAnd<RopSrc, RopDst>>(bwContentOutput, bwContentMaskOutput);
  }
  if (m_inputs.dbg) {
    m_inputs.dbg->add(dst, "fillZones");
  }
  m_inputs.status.throwIfCancelled();

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
}  // namespace output
