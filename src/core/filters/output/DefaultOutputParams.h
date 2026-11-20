// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_DEFAULTOUTPUTPARAMS_H_
#define SCANTAILOR_OUTPUT_DEFAULTOUTPUTPARAMS_H_

#include <Dpi.h>

#include <QDomDocument>
#include <QDomElement>

#include "ColorParams.h"
#include "DepthPerception.h"
#include "DespeckleLevel.h"
#include "DewarpingOptions.h"
#include "PictureShapeOptions.h"
#include "SplittingOptions.h"

namespace output {
class DefaultOutputParams {
 public:
  DefaultOutputParams();

  DefaultOutputParams(const Dpi& dpi,
                      const ColorParams& colorParams,
                      const SplittingOptions& splittingOptions,
                      const PictureShapeOptions& pictureShapeOptions,
                      const DepthPerception& depthPerception,
                      const DewarpingOptions& dewarpingOptions,
                      double despeckleLevel);

  explicit DefaultOutputParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  const Dpi& getDpi() const;

  void setDpi(const Dpi& dpi);

  const ColorParams& getColorParams() const;

  void setColorParams(const ColorParams& colorParams);

  const SplittingOptions& getSplittingOptions() const;

  void setSplittingOptions(const SplittingOptions& splittingOptions);

  const PictureShapeOptions& getPictureShapeOptions() const;

  void setPictureShapeOptions(const PictureShapeOptions& pictureShapeOptions);

  const DepthPerception& getDepthPerception() const;

  void setDepthPerception(const DepthPerception& depthPerception);

  const DewarpingOptions& getDewarpingOptions() const;

  void setDewarpingOptions(const DewarpingOptions& dewarpingOptions);

  double getDespeckleLevel() const;

  void setDespeckleLevel(double despeckleLevel);

 private:
  Dpi m_dpi;
  ColorParams m_colorParams;
  SplittingOptions m_splittingOptions;
  PictureShapeOptions m_pictureShapeOptions;
  DepthPerception m_depthPerception;
  DewarpingOptions m_dewarpingOptions;
  double m_despeckleLevel;
};
}  // namespace output

#endif  // ifndef SCANTAILOR_OUTPUT_DEFAULTOUTPUTPARAMS_H_