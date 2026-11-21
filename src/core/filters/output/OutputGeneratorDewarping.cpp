// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGenerator.h"

#include <AdjustBrightness.h>
#include <ColoredDimming.h>
#include <Curve.h>
#include <CylindricalSurfaceDewarper.h>
#include <DewarpingPointMapper.h>
#include <DistortionModel.h>
#include <DistortionModelBuilder.h>
#include <Grayscale.h>
#include <RasterDewarper.h>
#include <RasterOp.h>
#include <TextLineTracer.h>
#include <TopBottomEdgeTracer.h>
#include <Transform.h>
#include <WidthCorrection.h>
#include <WidthProfile.h>
#include <XSpline.h>
#include <imageproc/BackgroundColorCalculator.h>
#include <imageproc/ImageCombination.h>
#include <imageproc/RotateOrthogonally.h>
#include <imageproc/SkewFinder.h>

#include <QLineF>
#include <QPolygonF>
#include <QTransform>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

#include "DebugImages.h"
#include "DepthPerception.h"
#include "OutputGeneratorProcessor.h"
#include "OutputImageBuilder.h"
#include "OutputImageOps.h"
#include "OutputZoneOps.h"
#include "Settings.h"
#include "TaskStatus.h"
#include "Utils.h"
#include "VecNT.h"

using namespace imageproc;
using namespace dewarping;
namespace output {
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
    warpedGrayOutput
        = transformToGray(m_images.inputGrayImage, m_inputs.xform.transform(), m_geometry.workingBoundingRect,
                          OutsidePixels::assumeWeakColor(m_outsideBackgroundColor));
  } else {
    warpedGrayOutput = normalizeIlluminationGray(m_images.inputGrayImage, m_geometry.preCropAreaInOriginalCs,
                                                 m_inputs.xform.transform(), m_geometry.workingBoundingRect);
  }

  // Original image, but:
  // 1. In a format we can handle, that is grayscale, RGB32, ARGB32
  // 2. With illumination normalized over the content area, if required.
  // 3. With margins filled with white, if required.
  QImage normalizedOriginal;
  const QTransform workingToOrig
      = QTransform().translate(m_geometry.workingBoundingRect.left(), m_geometry.workingBoundingRect.top())
        * m_inputs.xform.transformBack();
  if (!m_renderParams.normalizeIllumination()) {
    normalizedOriginal = (m_images.colorOriginal) ? m_images.inputOrigImage : m_images.inputGrayImage;
  } else {
    GrayImage normalizedGray = transformToGray(warpedGrayOutput, workingToOrig, m_images.inputOrigImage.rect(),
                                               OutsidePixels::assumeWeakColor(m_outsideBackgroundColor));
    if (!m_images.colorOriginal) {
      normalizedOriginal = normalizedGray;
    } else {
      normalizedOriginal = m_images.inputOrigImage;
      adjustBrightnessGrayscale(normalizedOriginal, normalizedGray);
      if (m_inputs.dbg) {
        m_inputs.dbg->add(normalizedOriginal, "norm_illum_color");
      }
    }

    m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
        normalizedOriginal, m_geometry.outCropAreaInOriginalCs);
  }

  m_inputs.status.throwIfCancelled();

  // Picture mask (white indicate a picture) in the same coordinates as
  // warpedGrayOutput. Only built for Mixed mode.
  BinaryImage warpedBwMask;
  if (m_renderParams.mixedOutput()) {
    warpedBwMask = BinaryImage(m_geometry.workingBoundingRect.size(), BLACK);
    processPictureZones(warpedBwMask, pictureZones, warpedGrayOutput);
    if (m_inputs.dbg) {
      m_inputs.dbg->add(warpedBwMask, "warpedBwMask");
    }
    m_inputs.status.throwIfCancelled();

    if (autoPictureMask) {
      if (autoPictureMask->size() != m_geometry.targetSize) {
        BinaryImage(m_geometry.targetSize).swap(*autoPictureMask);
      }
      autoPictureMask->fill(BLACK);
      rasterOp<RopSrc>(*autoPictureMask, m_geometry.croppedContentRect, warpedBwMask,
                       m_geometry.contentRectInWorkingCs.topLeft());
    }
    m_inputs.status.throwIfCancelled();

    // BW mask begin
    BinaryImage warpedBwContent;
    QImage maybeSmoothed;
    if (!m_renderParams.needSavitzkyGolaySmoothing()) {
      maybeSmoothed = warpedGrayOutput;
    } else {
      maybeSmoothed = detail::smoothToGrayscale(warpedGrayOutput, m_dpi);
      if (m_inputs.dbg) {
        m_inputs.dbg->add(maybeSmoothed, "smoothed");
      }
      m_inputs.status.throwIfCancelled();
    }

    warpedBwContent = binarize(maybeSmoothed, m_geometry.contentAreaInWorkingCs);
    maybeSmoothed = QImage();  // Save memory.
    if (m_inputs.dbg) {
      m_inputs.dbg->add(warpedBwContent, "binarized_and_cropped");
    }
    m_inputs.status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(warpedBwContent);
      m_inputs.status.throwIfCancelled();
    }
    // BW mask end

    modifyBinarizationMask(warpedBwMask, warpedBwContent, m_geometry.workingBoundingRect, pictureZones);
    warpedBwContent.release();  // Save memory.
    if (m_inputs.dbg) {
      m_inputs.dbg->add(warpedBwMask, "warpedBwMask with zones");
    }
    m_inputs.status.throwIfCancelled();
  }

  // REVIEW-NOTE (2026-11): with "Normalize illumination" on, the background is estimated over the working area,
  // which is the whole page when the margins are not filled -> on a page with uneven light and a dark scan edge the
  // gray values cut out for the tracing differ, and an edge of the model can move by about a millimetre. Not fixed
  // because normalizing the traced area once more costs time and the pages measured kept their decision.
  const auto pageToTrace
      = [&] { return GrayImage(warpedGrayOutput.toQImage().copy(m_geometry.tracingRectInWorkingCs)); };
  const QTransform tracingToOrig
      = QTransform::fromTranslate(m_geometry.tracingRectInWorkingCs.left(), m_geometry.tracingRectInWorkingCs.top())
        * workingToOrig;

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
    dewarped = dewarp(QTransform(), normalizedOriginal, m_inputs.xform.transform(), distortionModel, depthPerception,
                      m_outsideBackgroundColor);
    // A page reported as not dewarped stays as it is, also in its width.
    if (m_dewarpingOptions.evenOutLetterWidths() && !m_dewarpingOptions.noModelFound()) {
      evenOutLetterWidths(distortionModel, depthPerception, normalizedOriginal, dewarped);
    }

    const double deskewAngle = -findSkew(dewarped);
    rotateXform = Utils::rotate(deskewAngle, m_inputs.outRect);
    m_dewarpingOptions.setPostDeskewAngle(deskewAngle);
  } catch (const std::runtime_error&) {
    // Probably an impossible distortion model. Let's fall back to a trivial one.
    setupTrivialDistortionModel(distortionModel);
    // evenOutLetterWidths() may already have stored a correction measured with the failed model.
    distortionModel.setWidthCorrection(WidthCorrection());
    m_dewarpingOptions.setPostDeskewAngle(.0);

    dewarped = dewarp(QTransform(), normalizedOriginal, m_inputs.xform.transform(), distortionModel, depthPerception,
                      m_outsideBackgroundColor);
  }
  normalizedOriginal = QImage();  // Save memory.
  m_inputs.settings->setDewarpingOptions(m_inputs.pageId, m_dewarpingOptions);
  detail::applyAffineTransform(dewarped, rotateXform, m_outsideBackgroundColor);
  // As without dewarping, the filters work on the page in output resolution.
  applyEnhancementFilters(dewarped);
  if (m_inputs.dbg) {
    m_inputs.dbg->add(dewarped, "dewarped");
  }
  m_inputs.status.throwIfCancelled();

  // The same model domain as in dewarp(), or the zones land off their place on the dewarped page.
  auto mapper = std::make_shared<DewarpingPointMapper>(distortionModel, depthPerception.value(),
                                                       m_inputs.xform.transform(), m_inputs.outRect, rotateXform);
  const std::function<QPointF(const QPointF&)> origToOutput(
      std::bind(&DewarpingPointMapper::mapToDewarpedSpace, mapper, std::placeholders::_1));

  BinaryImage dewarpingContentAreaMask(m_images.inputGrayImage.size(), BLACK);
  {
    detail::fillMarginsInPlace(dewarpingContentAreaMask, m_geometry.contentAreaInOriginalCs, WHITE);
    dewarpingContentAreaMask
        = BinaryImage(dewarp(QTransform(), dewarpingContentAreaMask.toQImage(), m_inputs.xform.transform(),
                             distortionModel, depthPerception, Qt::white));
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
      if (m_inputs.dbg) {
        m_inputs.dbg->add(dewarpedAndMaybeSmoothed, "smoothed");
      }
      m_inputs.status.throwIfCancelled();
    }

    // don't destroy as it's needed for color segmentation
    if (!m_renderParams.needColorSegmentation()) {
      dewarped = QImage();
    }

    BinaryImage dewarpedBwContent = binarize(dewarpedAndMaybeSmoothed, dewarpingContentAreaMask);
    dewarpedAndMaybeSmoothed = QImage();  // Save memory.
    if (m_inputs.dbg) {
      m_inputs.dbg->add(dewarpedBwContent, "dewarpedBwContent");
    }
    m_inputs.status.throwIfCancelled();

    if (m_renderParams.needMorphologicalSmoothing()) {
      morphologicalSmoothInPlace(dewarpedBwContent);
      m_inputs.status.throwIfCancelled();
    }

    // It's important to keep despeckling the very last operation
    // affecting the binary part of the output. That's because
    // we will be reconstructing the input to this despeckling
    // operation from the final output file.
    maybeDespeckleInPlace(dewarpedBwContent, m_inputs.outRect, m_inputs.outRect, m_despeckleLevel, specklesImage,
                          m_dpi);

    if (!m_renderParams.needColorSegmentation()) {
      if (!m_images.blackOnWhite) {
        dewarpedBwContent.invert();
      }

      detail::applyFillZonesInPlace(dewarpedBwContent, fillZones, origToOutput);

      imageBuilder.setImage(dewarpedBwContent.toQImage());
    } else {
      QImage segmentedImage = colorText(dewarpedBwContent, dewarped);
      dewarped = QImage();
      dewarpedBwContent.release();


      if (!m_images.blackOnWhite) {
        segmentedImage.invertPixels();
      }

      detail::applyFillZonesInPlace(segmentedImage, fillZones, origToOutput, false);
      if (m_inputs.dbg) {
        m_inputs.dbg->add(segmentedImage, "segmented_with_fill_zones");
      }
      m_inputs.status.throwIfCancelled();

      imageBuilder.setImage(segmentedImage);
    }
    return imageBuilder.build();
  }

  BinaryImage dewarpedBwMask;
  BinaryImage dewarpedBwContent;
  if (m_renderParams.mixedOutput()) {
    const QTransform origToWorkingCs
        = m_inputs.xform.transform()
          * QTransform().translate(-m_geometry.workingBoundingRect.left(), -m_geometry.workingBoundingRect.top());
    QTransform workingToOutputCs
        = QTransform().translate(m_geometry.workingBoundingRect.left(), m_geometry.workingBoundingRect.top());
    dewarpedBwMask = BinaryImage(dewarp(origToWorkingCs, warpedBwMask.toQImage(), workingToOutputCs, distortionModel,
                                        depthPerception, Qt::black));
    warpedBwMask.release();
    detail::applyAffineTransform(dewarpedBwMask, rotateXform, BLACK);
    detail::fillMarginsInPlace(dewarpedBwMask, dewarpingContentAreaMask, BLACK);
    if (m_inputs.dbg) {
      m_inputs.dbg->add(dewarpedBwMask, "dewarpedBwMask");
    }
    m_inputs.status.throwIfCancelled();

    if (m_renderParams.needBinarization()) {
      QImage dewarpedAndMaybeSmoothed;
      if (!m_renderParams.needSavitzkyGolaySmoothing()) {
        dewarpedAndMaybeSmoothed = dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming());
      } else {
        dewarpedAndMaybeSmoothed = detail::smoothToGrayscale(
            dimColoredPixels(dewarped, m_colorParams.blackWhiteOptions().getColoredDimming()), m_dpi);
        if (m_inputs.dbg) {
          m_inputs.dbg->add(dewarpedAndMaybeSmoothed, "smoothed");
        }
        m_inputs.status.throwIfCancelled();
      }

      BinaryImage dewarpedBwMaskFilled(dewarpedBwMask);
      detail::fillMarginsInPlace(dewarpedBwMaskFilled, dewarpingContentAreaMask, WHITE);
      dewarpedBwContent = binarize(dewarpedAndMaybeSmoothed, dewarpedBwMaskFilled);
      dewarpedBwMaskFilled.release();
      dewarpedAndMaybeSmoothed = QImage();  // Save memory.
      if (m_inputs.dbg) {
        m_inputs.dbg->add(dewarpedBwContent, "dewarpedBwContent");
      }
      m_inputs.status.throwIfCancelled();

      if (m_renderParams.needMorphologicalSmoothing()) {
        morphologicalSmoothInPlace(dewarpedBwContent);
      }

      // It's important to keep despeckling the very last operation
      // affecting the binary part of the output. That's because
      // we will be reconstructing the input to this despeckling
      // operation from the final output file.
      maybeDespeckleInPlace(dewarpedBwContent, m_inputs.outRect, m_geometry.croppedContentRect, m_despeckleLevel,
                            specklesImage, m_dpi);

      if (m_renderParams.originalBackground()) {
        // See processWithoutDewarping(): keep the binarized content out of picture areas.
        rasterOp<RopAnd<RopSrc, RopDst>>(dewarpedBwContent, dewarpedBwMask);
      }

      if (!m_renderParams.normalizeIlluminationColor()) {
        m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
            m_images.colorOriginal ? m_images.inputOrigImage : m_images.inputGrayImage,
            m_geometry.outCropAreaInOriginalCs);

        {
          QImage origWithoutIllumination;
          if (m_images.colorOriginal) {
            origWithoutIllumination = m_images.inputOrigImage;
          } else {
            origWithoutIllumination = m_images.inputGrayImage;
          }
          dewarped = dewarp(QTransform(), origWithoutIllumination, m_inputs.xform.transform(), distortionModel,
                            depthPerception, m_outsideBackgroundColor);
        }
        detail::applyAffineTransform(dewarped, rotateXform, m_outsideBackgroundColor);
        applyEnhancementFilters(dewarped);
        m_inputs.status.throwIfCancelled();
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

      if (m_inputs.dbg) {
        m_inputs.dbg->add(dewarped, "combined");
      }

      if (m_renderParams.originalBackground()) {
        detail::reserveBlackAndWhite(dewarped, dewarpedBwContent.inverted());
      } else {
        detail::reserveBlackAndWhite(dewarped, dewarpedBwMask.inverted());
      }
      m_inputs.status.throwIfCancelled();
    }
  }

  applyPhotoAdjustments(dewarped, m_renderParams.mixedOutput() ? &dewarpedBwMask : nullptr);

  // The same filling colors as without dewarping; "Black" was missing here.
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
  detail::fillMarginsInPlace(dewarped, dewarpingContentAreaMask, m_outsideBackgroundColor);

  if (!m_images.blackOnWhite) {
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
  if (m_inputs.dbg) {
    m_inputs.dbg->add(dewarped, "fillZones");
  }
  m_inputs.status.throwIfCancelled();

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

/**
 * Set up a distortion model corresponding to the content rect,
 * which will result in no distortion correction.
 */
void detail::Processor::setupTrivialDistortionModel(DistortionModel& distortionModel) const {
  QPolygonF poly;
  if (!m_inputs.contentRect.isEmpty()) {
    poly = QRectF(m_inputs.contentRect);
  } else {
    poly << m_inputs.contentRect.topLeft() + QPointF(-0.5, -0.5);
    poly << m_inputs.contentRect.topLeft() + QPointF(0.5, -0.5);
    poly << m_inputs.contentRect.topLeft() + QPointF(0.5, 0.5);
    poly << m_inputs.contentRect.topLeft() + QPointF(-0.5, 0.5);
  }
  poly = m_inputs.xform.transformBack().map(poly);

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
  const QRect modelDomain(distortionModel.modelDomain(dewarper, origToSrc * srcToOutput, m_inputs.outRect).toRect());
  if (modelDomain.isEmpty()) {
    // Same size as on the normal path, or the results don't match each other and the masks.
    GrayImage out(m_inputs.outRect.size());
    out.fill(0xff);  // white
    return out;
  }
  return RasterDewarper::dewarp(src, m_inputs.outRect.size(), dewarper, modelDomain, bgColor);
}

void detail::Processor::evenOutLetterWidths(DistortionModel& distortionModel,
                                            const DepthPerception& depthPerception,
                                            const QImage& src,
                                            QImage& dewarped) const {
  const std::vector<WidthProfile::Sample> samples
      = WidthProfile::measure(GrayImage(dewarped), m_geometry.croppedContentRect, m_dpi.horizontal());
  // The same model domain as in dewarp(), where output x maps to (x - left) / width, as in RasterDewarper.
  const CylindricalSurfaceDewarper dewarper(createDewarper(distortionModel, QTransform(), depthPerception.value()));
  const QRect modelDomain(distortionModel.modelDomain(dewarper, m_inputs.xform.transform(), m_inputs.outRect).toRect());
  const WidthCorrection correction = WidthProfile::toCorrection(samples, modelDomain.left(), modelDomain.width());
  if (correction.isNull()) {
    return;
  }
  m_inputs.status.throwIfCancelled();

  distortionModel.setWidthCorrection(correction);
  dewarped = dewarp(QTransform(), src, m_inputs.xform.transform(), distortionModel, depthPerception,
                    m_outsideBackgroundColor);
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

DistortionModel detail::Processor::buildTrapezoidDistortionModel(const GrayImage& warpedGrayOutput,
                                                                 const QTransform& toOriginal) {
  return straightModelInOriginal(
      TextLineTracer::traceTrapezoid(warpedGrayOutput, m_dpi, m_geometry.contentBoxInTracingCs, m_inputs.dbg),
      toOriginal);
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
  const std::optional<DistortionModel> flatPageModel = TextLineTracer::trace(
      warpedGrayOutput, m_dpi, m_geometry.contentBoxInTracingCs, modelBuilder, m_inputs.status, m_inputs.dbg);
  if (flatPageModel) {
    return straightModelInOriginal(*flatPageModel, toOriginal);
  }
  modelBuilder.transform(toOriginal);

  TopBottomEdgeTracer::trace(m_images.inputGrayImage, modelBuilder.verticalBounds(), modelBuilder, m_inputs.status,
                             m_inputs.dbg);

  DistortionModel distortionModel = modelBuilder.tryBuildModel(m_inputs.dbg, &m_images.inputGrayImage.toQImage());
  if (!distortionModel.isValid()) {
    m_dewarpingOptions.setNoModelFound(true);
    setupTrivialDistortionModel(distortionModel);
  }
  return distortionModel;
}

DistortionModel detail::Processor::buildMarginalDistortionModel() const {
  BinaryImage bwImage(m_images.inputGrayImage, BinaryThreshold(64));

  QTransform transform = m_inputs.xform.preRotation().transform(bwImage.size());
  QTransform invTransform = transform.inverted();

  int degrees = m_inputs.xform.preRotation().toDegrees();
  bwImage = orthogonalRotation(bwImage, degrees);

  DistortionModel distortionModel;
  setupTrivialDistortionModel(distortionModel);

  int maxRedPoints = 5;
  XSpline topSpline;

  const std::vector<QPointF>& topPolyline = distortionModel.topCurve().polyline();

  const QLineF topLine(transform.map(topPolyline.front()), transform.map(topPolyline.back()));

  topSpline.appendControlPoint(topLine.p1(), 0);

  if ((m_inputs.pageId.subPage() == PageId::SINGLE_PAGE) || (m_inputs.pageId.subPage() == PageId::LEFT_PAGE)) {
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

  if ((m_inputs.pageId.subPage() == PageId::SINGLE_PAGE) || (m_inputs.pageId.subPage() == PageId::LEFT_PAGE)) {
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
  if (m_inputs.dbg) {
    QImage outImage(bwImage.toQImage().convertToFormat(QImage::Format_RGB32));
    for (int i = 0; i <= topSpline.numSegments(); i++) {
      detail::drawPoint(outImage, topSpline.controlPointPosition(i));
    }
    for (int i = 0; i <= bottomSpline.numSegments(); i++) {
      detail::drawPoint(outImage, bottomSpline.controlPointPosition(i));
    }
    m_inputs.dbg->add(outImage, "marginal dewarping");
  }
  return distortionModel;
}
}  // namespace output
