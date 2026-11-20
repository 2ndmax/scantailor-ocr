// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTGENERATORPROCESSOR_H_
#define SCANTAILOR_OUTPUT_OUTPUTGENERATORPROCESSOR_H_

#include <BinaryImage.h>
#include <BinaryThreshold.h>
#include <Dpi.h>
#include <GrayImage.h>

#include <QColor>
#include <QImage>
#include <QPolygonF>
#include <QRect>
#include <QSize>
#include <QTransform>
#include <memory>

#include "ColorParams.h"
#include "DewarpingOptions.h"
#include "ForegroundType.h"
#include "ImageTransformation.h"
#include "OutputImage.h"
#include "OutputProcessingParams.h"
#include "PageId.h"
#include "PictureShapeOptions.h"
#include "RenderParams.h"
#include "SplittingOptions.h"

class DebugImages;
class FilterData;
class PageId;
class TaskStatus;
class ZoneSet;

namespace dewarping {
class CylindricalSurfaceDewarper;
class DistortionModel;
}  // namespace dewarping

namespace output {
class DepthPerception;
class OutputGenerator;
class Settings;

namespace detail {
using dewarping::CylindricalSurfaceDewarper;
using dewarping::DistortionModel;
using imageproc::BinaryImage;
using imageproc::BinaryThreshold;
using imageproc::GrayImage;

struct ProcessorInputs {
  const ImageTransformation& xform;
  const QRect& outRect;
  const QRect& contentRect;
  const PageId pageId;
  const std::shared_ptr<Settings> settings;
  const bool takeOverBlackOnWhite;
  const TaskStatus& status;
  DebugImages* const dbg;
};

struct AreaGeometry {
  QPolygonF preCropArea;
  QPolygonF croppedContentArea;
  QRect croppedContentRect;
  QPolygonF outCropArea;
  QSize targetSize;
  QRect workingBoundingRect;
  QRect contentRectInWorkingCs;
  QRect tracingRectInWorkingCs;
  QRect contentBoxInTracingCs;
  QPolygonF contentAreaInWorkingCs;
  QPolygonF outCropAreaInWorkingCs;
  QPolygonF preCropAreaInOriginalCs;
  QPolygonF contentAreaInOriginalCs;
  QPolygonF outCropAreaInOriginalCs;
};

struct InputImages {
  bool blank = false;
  bool blackOnWhite = true;
  QImage inputOrigImage;
  GrayImage inputGrayImage;
  bool colorOriginal = false;
};

class Processor {
 public:
  Processor(const OutputGenerator& generator,
            const PageId& pageId,
            const std::shared_ptr<Settings>& settings,
            bool takeOverBlackOnWhite,
            const FilterData& input,
            const TaskStatus& status,
            DebugImages* dbg);

  std::unique_ptr<OutputImage> process(ZoneSet& pictureZones,
                                       const ZoneSet& fillZones,
                                       DistortionModel& distortionModel,
                                       const DepthPerception& depthPerception,
                                       BinaryImage* autoPictureMask,
                                       BinaryImage* specklesImage);

 private:
  void initParams();

  void calcAreas();

  void updateBlackOnWhite(const FilterData& input);

  void initFilterData(const FilterData& input);

  std::unique_ptr<OutputImage> processImpl(ZoneSet& pictureZones,
                                           const ZoneSet& fillZones,
                                           DistortionModel& distortionModel,
                                           const DepthPerception& depthPerception,
                                           BinaryImage* autoPictureMask,
                                           BinaryImage* specklesImage);

  std::unique_ptr<OutputImage> processWithoutDewarping(ZoneSet& pictureZones,
                                                       const ZoneSet& fillZones,
                                                       BinaryImage* autoPictureMask,
                                                       BinaryImage* specklesImage);

  std::unique_ptr<OutputImage> processWithDewarping(ZoneSet& pictureZones,
                                                    const ZoneSet& fillZones,
                                                    DistortionModel& distortionModel,
                                                    const DepthPerception& depthPerception,
                                                    BinaryImage* autoPictureMask,
                                                    BinaryImage* specklesImage);

  std::unique_ptr<OutputImage> buildEmptyImage() const;

  double findSkew(const QImage& image) const;

  void setupTrivialDistortionModel(DistortionModel& distortionModel) const;

  static CylindricalSurfaceDewarper createDewarper(const DistortionModel& distortionModel,
                                                   const QTransform& distortionModelToTarget,
                                                   double depthPerception);

  QImage dewarp(const QTransform& origToSrc,
                const QImage& src,
                const QTransform& srcToOutput,
                const DistortionModel& distortionModel,
                const DepthPerception& depthPerception,
                const QColor& bgColor) const;

  /**
   * Measures the letter widths on the dewarped page. If they differ enough,
   * stores a width correction in the distortion model and dewarps src again.
   */
  void evenOutLetterWidths(DistortionModel& distortionModel,
                           const DepthPerception& depthPerception,
                           const QImage& src,
                           QImage& dewarped) const;

  GrayImage normalizeIlluminationGray(const QImage& input,
                                      const QPolygonF& areaToConsider,
                                      const QTransform& xform,
                                      const QRect& targetRect,
                                      GrayImage* background = nullptr) const;

  GrayImage detectPictures(const GrayImage& input300dpi) const;

  BinaryImage estimateBinarizationMask(const GrayImage& graySource, const QRect& sourceRect) const;

  void modifyBinarizationMask(BinaryImage& bwMask,
                              BinaryImage& bwContent,
                              const QRect& maskRect,
                              const ZoneSet& zones) const;

  BinaryThreshold adjustThreshold(BinaryThreshold threshold) const;

  BinaryThreshold calcBinarizationThreshold(const QImage& image, const BinaryImage& mask) const;

  BinaryThreshold calcBinarizationThreshold(const QImage& image,
                                            const QPolygonF& cropArea,
                                            const BinaryImage* mask = nullptr) const;

  void morphologicalSmoothInPlace(BinaryImage& binImg) const;

  BinaryImage binarize(const QImage& image) const;

  BinaryImage binarize(const QImage& image, const BinaryImage& mask) const;

  BinaryImage binarize(const QImage& image, const QPolygonF& cropArea, const BinaryImage* mask = nullptr) const;

  void maybeDespeckleInPlace(BinaryImage& image,
                             const QRect& imageRect,
                             const QRect& maskRect,
                             double level,
                             BinaryImage* specklesImg,
                             const Dpi& dpi) const;

  QImage segmentImage(const BinaryImage& image, const QImage& colorImage) const;

  QImage posterizeImage(const QImage& image, const QColor& backgroundColor = Qt::white) const;

  /// The text in color, as set: K-means on the colors of the text and the segments of its letters,
  /// or color segmentation, posterized if set.
  QImage colorText(const BinaryImage& text, const QImage& colorImage) const;

  ForegroundType getForegroundType() const;

  void processPictureZones(BinaryImage& mask, ZoneSet& pictureZones, const GrayImage& image);

  QImage transformToWorkingCs(bool normalize) const;

  // The filters under "Enhancement", applied to the image in output resolution.
  void applyEnhancementFilters(QImage& image) const;

  // The photo adjustments, applied before the margins are filled. With a picture mask (mixed
  // output), only the pictures are changed.
  void applyPhotoAdjustments(QImage& image, const BinaryImage* pictureMask) const;

  DistortionModel buildAutoDistortionModel(const GrayImage& warpedGrayOutput, const QTransform& toOriginal);

  DistortionModel buildTrapezoidDistortionModel(const GrayImage& warpedGrayOutput, const QTransform& toOriginal);

  /**
   * Takes a model of straight lines from the traced page to the original image. An invalid one, of a page with
   * too few text lines, becomes the trivial model, and the page is reported as not dewarped.
   */
  DistortionModel straightModelInOriginal(DistortionModel distortionModel, const QTransform& toOriginal);

  DistortionModel buildMarginalDistortionModel() const;

  ProcessorInputs m_inputs;

  Dpi m_dpi;
  ColorParams m_colorParams;
  SplittingOptions m_splittingOptions;
  PictureShapeOptions m_pictureShapeOptions;
  DewarpingOptions m_dewarpingOptions;
  double m_despeckleLevel;
  OutputProcessingParams m_outputProcessingParams;

  RenderParams m_renderParams;

  AreaGeometry m_geometry;

  InputImages m_images;

  QColor m_outsideBackgroundColor;
};
}  // namespace detail
}  // namespace output

#endif  // SCANTAILOR_OUTPUT_OUTPUTGENERATORPROCESSOR_H_
