// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputGenerator.h"

#include <AdjustBrightness.h>
#include <ConnectivityMap.h>
#include <DistortionModel.h>
#include <GrayRasterOp.h>
#include <Grayscale.h>
#include <Morphology.h>
#include <PictureDetection.h>
#include <PolygonRasterizer.h>
#include <PolynomialSurface.h>
#include <RasterOp.h>
#include <Scale.h>
#include <Transform.h>
#include <imageproc/BackgroundColorCalculator.h>
#include <imageproc/ChromaDenoiser.h>
#include <imageproc/ColorKMeans.h>
#include <imageproc/ColorSegmenter.h>
#include <imageproc/KnnDenoiser.h>
#include <imageproc/PolygonUtils.h>
#include <imageproc/Posterizer.h>
#include <imageproc/Sharpen.h>
#include <imageproc/SubtractBackground.h>
#include <imageproc/WienerFilter.h>

#include <QColor>
#include <QPainterPath>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <QTransform>
#include <new>
#include <vector>

#include "BlackWhiteBinarization.h"
#include "DebugImages.h"
#include "Despeckle.h"
#include "Dpm.h"
#include "EstimateBackground.h"
#include "FilterData.h"
#include "ImageSizeLimit.h"
#include "OutputGeneratorProcessor.h"
#include "OutputImageBuilder.h"
#include "OutputImageOps.h"
#include "OutputMorphologyOps.h"
#include "OutputRectFinder.h"
#include "OutputZoneOps.h"
#include "Params.h"
#include "PictureLayerProperty.h"
#include "Settings.h"
#include "TaskStatus.h"

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
    : m_inputs{generator.m_xform,
               generator.m_outRect,
               generator.m_contentRect,
               pageId,
               settings,
               takeOverBlackOnWhite,
               status,
               dbg},
      m_despeckleLevel(0),
      m_images{false, true, QImage(), GrayImage(QSize()), false} {
  initParams();
  calcAreas();
  if (!m_images.blank) {
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
  const Params params = m_inputs.settings->getParams(m_inputs.pageId);
  m_dpi = params.outputDpi();
  m_colorParams = params.colorParams();
  m_splittingOptions = params.splittingOptions();
  m_pictureShapeOptions = params.pictureShapeOptions();
  m_dewarpingOptions = params.dewarpingOptions();
  m_despeckleLevel = params.despeckleLevel();

  m_outputProcessingParams = m_inputs.settings->getOutputProcessingParams(m_inputs.pageId);

  m_renderParams = RenderParams(m_colorParams, m_splittingOptions);
}

void detail::Processor::calcAreas() {
  m_geometry.targetSize = outputSizeOf(m_inputs.outRect);
  // The limit for images also holds for the output, as it is processed the same way. Without it,
  // a high output resolution or a too low DPI of the image could make the output arbitrarily large.
  if (ImageSizeLimit::exceeds(m_geometry.targetSize)) {
    throw OutputTooLargeError(m_geometry.targetSize);
  }

  if (m_renderParams.fillOutsidePageBox()) {
    m_geometry.preCropArea = QRectF(m_inputs.outRect);
  } else if (m_renderParams.fillOffcut()) {
    m_geometry.preCropArea = m_inputs.xform.resultingPreCropArea();
  } else {
    const QPolygonF imageRectInOutputCs = m_inputs.xform.transform().map(m_inputs.xform.origRect());
    m_geometry.preCropArea = imageRectInOutputCs.intersected(QRectF(m_inputs.outRect));
  }

  m_geometry.croppedContentArea = m_geometry.preCropArea.intersected(
      QRectF(m_renderParams.fillMargins() ? m_inputs.contentRect : m_inputs.outRect));
  m_geometry.croppedContentRect = m_geometry.croppedContentArea.boundingRect().toRect();
  m_geometry.croppedContentArea
      = PolygonUtils::round(m_geometry.croppedContentArea.intersected(QRectF(m_geometry.croppedContentRect)));
  if (m_geometry.croppedContentRect.isEmpty()) {
    m_images.blank = true;
    return;
  }

  m_geometry.outCropArea = m_geometry.preCropArea.intersected(QRectF(m_inputs.outRect));

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
  m_geometry.workingBoundingRect = m_geometry.preCropArea
                                       .intersected(QRectF(m_geometry.croppedContentRect.adjusted(
                                           -contentMargin, -contentMargin, contentMargin, contentMargin)))
                                       .boundingRect()
                                       .toRect();
  m_geometry.contentRectInWorkingCs
      = m_geometry.croppedContentRect.translated(-m_geometry.workingBoundingRect.topLeft());
  // Whether and how a page is dewarped must not depend on "Fill margins". So the text lines are always traced in
  // the content box and its margin, which is the working area when the margins are filled. Without a content box
  // there is only the whole page to trace.
  const QRect contentBox = m_geometry.preCropArea.intersected(QRectF(m_inputs.contentRect)).boundingRect().toRect();
  const QRect tracedBox = contentBox.isEmpty() ? m_geometry.croppedContentRect : contentBox;
  const QRect tracingRect
      = m_geometry.preCropArea
            .intersected(QRectF(tracedBox.adjusted(-contentMargin, -contentMargin, contentMargin, contentMargin)))
            .boundingRect()
            .toRect()
        & m_geometry.workingBoundingRect;
  m_geometry.tracingRectInWorkingCs = tracingRect.translated(-m_geometry.workingBoundingRect.topLeft());
  m_geometry.contentBoxInTracingCs = tracedBox.translated(-tracingRect.topLeft());
  m_geometry.contentAreaInWorkingCs
      = m_geometry.croppedContentArea.translated(-m_geometry.workingBoundingRect.topLeft());
  m_geometry.outCropAreaInWorkingCs = m_geometry.outCropArea.translated(-m_geometry.workingBoundingRect.topLeft());
  m_geometry.preCropAreaInOriginalCs = m_inputs.xform.transformBack().map(m_geometry.preCropArea);
  m_geometry.contentAreaInOriginalCs = m_inputs.xform.transformBack().map(m_geometry.croppedContentArea);
  m_geometry.outCropAreaInOriginalCs = m_inputs.xform.transformBack().map(m_geometry.outCropArea);
}


void fillMarginsInPlace(QImage& image, const BinaryImage& contentMask, const QColor& color) {
  detail::fillMarginsInPlace(image, contentMask, color);
}

std::vector<QRect> findRectAreas(const BinaryImage& mask, int sensitivity) {
  return detail::findRectAreas(mask, sensitivity);
}


std::unique_ptr<OutputImage> detail::Processor::buildEmptyImage() const {
  OutputImageBuilder imageBuilder;

  BinaryImage emptyImage(m_geometry.targetSize, WHITE);
  if (!m_renderParams.splitOutput()) {
    imageBuilder.setImage(emptyImage.toQImage());
  } else {
    // The split layers are cut out with applyMask(), which rejects 1-bit images,
    // so use a white 8-bit grayscale image here.
    GrayImage whiteImage(m_geometry.targetSize);
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
  bool blackOnWhite = m_inputs.settings->getParams(m_inputs.pageId).isBlackOnWhite();
  if (m_inputs.takeOverBlackOnWhite
      && !m_inputs.settings->getOutputProcessingParams(m_inputs.pageId).isBlackOnWhiteSetManually()) {
    if (blackOnWhite != input.isBlackOnWhite()) {
      blackOnWhite = input.isBlackOnWhite();
      // Only this field: writing back all of the params would undo changes the user made to the
      // page meanwhile in the GUI thread.
      m_inputs.settings->setBlackOnWhite(m_inputs.pageId, blackOnWhite);
    }
  }
  m_images.blackOnWhite = blackOnWhite;
}

void detail::Processor::initFilterData(const FilterData& input) {
  updateBlackOnWhite(input);

  m_images.inputGrayImage = m_images.blackOnWhite ? input.grayImage() : input.grayImage().inverted();
  m_images.inputOrigImage = input.origImage();
  if ((m_images.inputOrigImage.format() != QImage::Format_RGB32)
      && (m_images.inputOrigImage.format() != QImage::Format_ARGB32)
      && !((m_images.inputOrigImage.format() == QImage::Format_Indexed8) && m_images.inputOrigImage.isGrayscale())) {
    m_images.inputOrigImage = m_images.inputOrigImage.convertToFormat(
        m_images.inputOrigImage.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32);
  }
  if (!m_images.blackOnWhite) {
    m_images.inputOrigImage.invertPixels();
  }
  // With grayscale output, a color source is processed like a grayscale scan. Black and white
  // mode is left alone, as its color segmentation keeps colored text on purpose.
  const bool grayscaleOutput
      = m_colorParams.colorCommonOptions().isGrayscaleOutput() && (m_colorParams.colorMode() != BLACK_AND_WHITE);
  m_images.colorOriginal = !grayscaleOutput && !m_images.inputOrigImage.allGray();
}

std::unique_ptr<OutputImage> detail::Processor::processImpl(ZoneSet& pictureZones,
                                                            const ZoneSet& fillZones,
                                                            DistortionModel& distortionModel,
                                                            const DepthPerception& depthPerception,
                                                            BinaryImage* autoPictureMask,
                                                            BinaryImage* specklesImage) {
  if (m_images.blank) {
    if (autoPictureMask) {
      // A blank page has no picture areas. The mask is still needed for the picture zones tab.
      BinaryImage(m_geometry.targetSize, BLACK).swap(*autoPictureMask);
    }
    return buildEmptyImage();
  }

  m_outsideBackgroundColor = BackgroundColorCalculator::calcDominantBackgroundColor(
      m_images.colorOriginal ? m_images.inputOrigImage : m_images.inputGrayImage, m_geometry.outCropAreaInOriginalCs);

  if (m_dewarpingOptions.findsCurvesItself()
      || ((m_dewarpingOptions.dewarpingMode() == MANUAL) && distortionModel.isValid())) {
    return processWithDewarping(pictureZones, fillZones, distortionModel, depthPerception, autoPictureMask,
                                specklesImage);
  } else {
    return processWithoutDewarping(pictureZones, fillZones, autoPictureMask, specklesImage);
  }
}


GrayImage detail::Processor::normalizeIlluminationGray(const QImage& input,
                                                       const QPolygonF& areaToConsider,
                                                       const QTransform& xform,
                                                       const QRect& targetRect,
                                                       GrayImage* background) const {
  GrayImage toBeNormalized = transformToGray(input, xform, targetRect, OutsidePixels::assumeWeakNearest());
  if (m_inputs.dbg) {
    m_inputs.dbg->add(toBeNormalized, "toBeNormalized");
  }

  m_inputs.status.throwIfCancelled();

  QPolygonF transformedConsiderationArea = xform.map(areaToConsider);
  transformedConsiderationArea.translate(-targetRect.topLeft());

  const PolynomialSurface bgPs
      = estimateBackground(toBeNormalized, transformedConsiderationArea, m_inputs.status, m_inputs.dbg);
  m_inputs.status.throwIfCancelled();

  GrayImage bgImg(bgPs.render(toBeNormalized.size()));
  if (m_inputs.dbg) {
    m_inputs.dbg->add(bgImg, "background");
  }
  if (background) {
    *background = bgImg;
  }
  m_inputs.status.throwIfCancelled();

  grayRasterOp<detail::RaiseAboveBackground>(bgImg, toBeNormalized);
  if (m_inputs.dbg) {
    m_inputs.dbg->add(bgImg, "normalized_illumination");
  }
  m_inputs.status.throwIfCancelled();
  return bgImg;
}

BinaryImage detail::Processor::estimateBinarizationMask(const GrayImage& graySource, const QRect& sourceRect) const {
  const QSize downscaledSize(detail::to300dpi(graySource.size(), m_dpi));

  // A 300dpi version of graySource.
  GrayImage downscaledInput(scaleToGray(graySource, downscaledSize));
  m_inputs.status.throwIfCancelled();

  // Light areas indicate pictures.
  GrayImage pictureAreas(detectPictures(downscaledInput));
  downscaledInput = GrayImage();  // Save memory.
  m_inputs.status.throwIfCancelled();

  const BinaryThreshold threshold(48);
  // Scale back to original size.
  pictureAreas = scaleToGray(pictureAreas, sourceRect.size());
  return BinaryImage(pictureAreas, threshold);
}

void detail::Processor::modifyBinarizationMask(BinaryImage& bwMask,
                                               BinaryImage& bwContent,
                                               const QRect& maskRect,
                                               const ZoneSet& zones) const {
  QTransform xform = m_inputs.xform.transform();
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


GrayImage detail::Processor::detectPictures(const GrayImage& input300dpi) const {
  return ::detectPictures(input300dpi, m_pictureShapeOptions.isHigherSearchSensitivity(), m_inputs.status,
                          m_inputs.dbg);
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

  m_inputs.status.throwIfCancelled();

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

  m_inputs.status.throwIfCancelled();

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

  m_inputs.status.throwIfCancelled();

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

  m_inputs.status.throwIfCancelled();

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

  m_inputs.status.throwIfCancelled();

  {
    const char pattern[]
        = "   "
          "X+X"
          "XXX";
    detail::hitMissReplaceAllDirections(binImg, pattern, 3, 3);
  }

  if (m_inputs.dbg) {
    m_inputs.dbg->add(binImg, "edges_smoothed");
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
 *        coordinate system where m_inputs.contentRect and m_cropRect are defined.
 * \param maskRect The area within the image to consider. Defined not
 *        relative to \p image, but in the same coordinate system where
 *        m_inputs.contentRect and m_cropRect are defined. This only affects
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
    BinaryImage(m_inputs.outRect.size(), WHITE).swap(*specklesImg);
    if (!maskRect.isEmpty()) {
      rasterOp<RopSrc>(*specklesImg, dstRect, image, srcRect.topLeft());
    }
  }

  if (level != 0) {
    Despeckle::despeckleInPlace(image, dpi, level, m_inputs.status, m_inputs.dbg);

    if (m_inputs.dbg) {
      m_inputs.dbg->add(image, "despeckled");
    }
  }

  if (specklesImg) {
    if (!maskRect.isEmpty()) {
      rasterOp<RopSubtract<RopDst, RopSrc>>(*specklesImg, dstRect, image, srcRect.topLeft());
    }
  }

  m_inputs.status.throwIfCancelled();
}


QImage detail::Processor::segmentImage(const BinaryImage& image, const QImage& colorImage) const {
  const BlackWhiteOptions::ColorSegmenterOptions& segmenterOptions
      = m_colorParams.blackWhiteOptions().getColorSegmenterOptions();
  ColorSegmenter segmenter(m_dpi, segmenterOptions.getNoiseReduction(), segmenterOptions.getRedThresholdAdjustment(),
                           segmenterOptions.getGreenThresholdAdjustment(),
                           segmenterOptions.getBlueThresholdAdjustment());

  QImage segmented;
  {
    if (m_images.colorOriginal) {
      segmented = segmenter.segment(image, colorImage);
    } else {
      segmented = segmenter.segment(image, GrayImage(colorImage));
    }
  }

  if (m_inputs.dbg) {
    m_inputs.dbg->add(segmented, "segmented");
  }
  m_inputs.status.throwIfCancelled();
  return segmented;
}

QImage detail::Processor::posterizeImage(const QImage& image, const QColor& backgroundColor) const {
  const ColorCommonOptions::PosterizationOptions& posterizationOptions
      = m_colorParams.colorCommonOptions().getPosterizationOptions();
  Posterizer posterizer(posterizationOptions.getLevel(), posterizationOptions.isNormalizationEnabled(),
                        posterizationOptions.isForceBlackAndWhite(), 0, qRound(backgroundColor.lightnessF() * 255));

  QImage posterized = posterizer.posterize(image);

  if (m_inputs.dbg) {
    m_inputs.dbg->add(posterized, "posterized");
  }
  m_inputs.status.throwIfCancelled();
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
        = m_images.colorOriginal
              ? ColorSegmenter(m_dpi, options.getNoiseReduction(), options.getRedThresholdAdjustment(),
                               options.getGreenThresholdAdjustment(), options.getBlueThresholdAdjustment())
                    .segmentMap(text, colorImage)
              : ConnectivityMap(text, CONN8);
    m_inputs.status.throwIfCancelled();
    const int quarterInch = (m_dpi.horizontal() + m_dpi.vertical()) / 8;
    QImage colored
        = Posterizer::convertToIndexed(reduceTextColorsKMeans(colorImage, text, segments, options.getKMeansColors(),
                                                              options.isKMeansBlackFixed(), quarterInch * quarterInch));
    if (m_inputs.dbg) {
      m_inputs.dbg->add(colored, "k-means");
    }
    m_inputs.status.throwIfCancelled();
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
      mask = estimateBinarizationMask(image, m_geometry.workingBoundingRect);
    }

    detail::removeAutoPictureZones(pictureZones);
    m_inputs.settings->setPictureZones(m_inputs.pageId, pictureZones);
    m_outputProcessingParams.setAutoZonesFound(false);
    m_inputs.settings->setOutputProcessingParams(m_inputs.pageId, m_outputProcessingParams);
  }
  if ((m_pictureShapeOptions.getPictureShape() == RECTANGULAR_SHAPE) && !m_outputProcessingParams.isAutoZonesFound()) {
    std::vector<QRect> areas = detail::findRectAreas(mask, m_pictureShapeOptions.getSensitivity());

    const QTransform fromWorkingCs = [this]() {
      QTransform toWorkingCs = m_inputs.xform.transform();
      toWorkingCs *= QTransform().translate(-m_geometry.workingBoundingRect.x(), -m_geometry.workingBoundingRect.y());
      return toWorkingCs.inverted();
    }();

    for (const QRect& area : areas) {
      pictureZones.add(detail::createPictureZoneFromPoly(fromWorkingCs.map(QRectF(area))));
    }
    m_inputs.settings->setPictureZones(m_inputs.pageId, pictureZones);
    m_outputProcessingParams.setAutoZonesFound(true);
    m_inputs.settings->setOutputProcessingParams(m_inputs.pageId, m_outputProcessingParams);

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
  adjustments.applyInPlace(image, pictureMask, !m_images.blackOnWhite);
  if (pictureMask && m_renderParams.needBinarization()) {
    // Pure black and white mark the text of mixed output; the adjusted pictures must not use them.
    detail::reserveBlackAndWhite(image, pictureMask->inverted());
  }
  m_inputs.status.throwIfCancelled();
}

void detail::Processor::decideOutsideBackgroundColor() {
  // Both paths fill their margins before the image is inverted back for white-on-black pages.
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
}

QImage detail::Processor::transformToWorkingCs(bool normalize) const {
  QImage dst;
  if (normalize) {
    dst = normalizeIlluminationGray(m_images.inputGrayImage, m_geometry.preCropAreaInOriginalCs,
                                    m_inputs.xform.transform(), m_geometry.workingBoundingRect);
    if (m_images.colorOriginal) {
      assert(dst.format() == QImage::Format_Indexed8);
      QImage colorImg = transform(m_images.inputOrigImage, m_inputs.xform.transform(), m_geometry.workingBoundingRect,
                                  OutsidePixels::assumeColor(m_outsideBackgroundColor));
      adjustBrightnessGrayscale(colorImg, dst);
      dst = colorImg;
    }
  } else {
    if (!m_images.colorOriginal) {
      dst = transformToGray(m_images.inputGrayImage, m_inputs.xform.transform(), m_geometry.workingBoundingRect,
                            OutsidePixels::assumeColor(m_outsideBackgroundColor));
    } else {
      dst = transform(m_images.inputOrigImage, m_inputs.xform.transform(), m_geometry.workingBoundingRect,
                      OutsidePixels::assumeColor(m_outsideBackgroundColor));
    }
  }
  m_inputs.status.throwIfCancelled();
  return dst;
}

}  // namespace output
