// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DEFAULTPARAMS_H_
#define SCANTAILOR_CORE_DEFAULTPARAMS_H_


#include "filters/deskew/DeskewParams.h"
#include <filters/deskew/SkewDetection.h>
#include <filters/output/ColorParams.h>
#include <filters/output/DepthPerception.h>
#include <filters/output/DespeckleLevel.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/PictureShapeOptions.h>
#include <filters/page_layout/Alignment.h>
#include <filters/page_layout/OutputSizeOptions.h>
#include "filters/page_split/PageSplitParams.h"
#include <filters/page_split/LayoutType.h>
#include <filters/page_split/SplitDetectionMethod.h>
#include "filters/select_content/SelectContentParams.h"
#include <filters/select_content/DetectionOptions.h>

#include <QtCore/QRectF>

#include "AutoManualMode.h"
#include "filters/fix_orientation/FixOrientationParams.h"
#include "Dpi.h"
#include "Margins.h"
#include "OrthogonalRotation.h"
#include "ProcessingOptions.h"
#include "Units.h"

class DefaultParams {
 public:
  using FixOrientationParams = fix_orientation::FixOrientationParams;

  using DeskewParams = deskew::DeskewParams;

  using PageSplitParams = page_split::PageSplitParams;

  using SelectContentParams = select_content::SelectContentParams;

  class PageLayoutParams {
   public:
    PageLayoutParams();

    PageLayoutParams(const Margins& hardMargins, const page_layout::Alignment& alignment, bool autoMargins);

    explicit PageLayoutParams(const QDomElement& el);

    QDomElement toXml(QDomDocument& doc, const QString& name) const;

    const Margins& getHardMargins() const;

    void setHardMargins(const Margins& hardMargins);

    const page_layout::Alignment& getAlignment() const;

    void setAlignment(const page_layout::Alignment& alignment);

    bool isAutoMargins() const;

    void setAutoMargins(bool autoMargins);

    /**
     * The output size for new projects. Unlike in a project, the target size is in the
     * units of the profile, like the margins.
     */
    const page_layout::OutputSizeOptions& getOutputSizeOptions() const { return m_outputSizeOptions; }

    void setOutputSizeOptions(const page_layout::OutputSizeOptions& options) { m_outputSizeOptions = options; }

   private:
    Margins m_hardMargins;
    page_layout::Alignment m_alignment;
    bool m_autoMargins;
    page_layout::OutputSizeOptions m_outputSizeOptions;
  };

  class OutputParams {
   public:
    OutputParams();

    OutputParams(const Dpi& dpi,
                 const output::ColorParams& colorParams,
                 const output::SplittingOptions& splittingOptions,
                 const output::PictureShapeOptions& pictureShapeOptions,
                 const output::DepthPerception& depthPerception,
                 const output::DewarpingOptions& dewarpingOptions,
                 double despeckleLevel);

    explicit OutputParams(const QDomElement& el);

    QDomElement toXml(QDomDocument& doc, const QString& name) const;

    const Dpi& getDpi() const;

    void setDpi(const Dpi& dpi);

    const output::ColorParams& getColorParams() const;

    void setColorParams(const output::ColorParams& colorParams);

    const output::SplittingOptions& getSplittingOptions() const;

    void setSplittingOptions(const output::SplittingOptions& splittingOptions);

    const output::PictureShapeOptions& getPictureShapeOptions() const;

    void setPictureShapeOptions(const output::PictureShapeOptions& pictureShapeOptions);

    const output::DepthPerception& getDepthPerception() const;

    void setDepthPerception(const output::DepthPerception& depthPerception);

    const output::DewarpingOptions& getDewarpingOptions() const;

    void setDewarpingOptions(const output::DewarpingOptions& dewarpingOptions);

    double getDespeckleLevel() const;

    void setDespeckleLevel(double despeckleLevel);

   private:
    Dpi m_dpi;
    output::ColorParams m_colorParams;
    output::SplittingOptions m_splittingOptions;
    output::PictureShapeOptions m_pictureShapeOptions;
    output::DepthPerception m_depthPerception;
    output::DewarpingOptions m_dewarpingOptions;
    double m_despeckleLevel;
  };

 public:
  DefaultParams();

  DefaultParams(const FixOrientationParams& fixOrientationParams,
                const DeskewParams& deskewParams,
                const PageSplitParams& pageSplitParams,
                const SelectContentParams& selectContentParams,
                const PageLayoutParams& pageLayoutParams,
                const OutputParams& outputParams);

  explicit DefaultParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  const FixOrientationParams& getFixOrientationParams() const;

  void setFixOrientationParams(const FixOrientationParams& fixOrientationParams);

  const DeskewParams& getDeskewParams() const;

  void setDeskewParams(const DeskewParams& deskewParams);

  const PageSplitParams& getPageSplitParams() const;

  void setPageSplitParams(const PageSplitParams& pageSplitParams);

  const SelectContentParams& getSelectContentParams() const;

  void setSelectContentParams(const SelectContentParams& selectContentParams);

  const PageLayoutParams& getPageLayoutParams() const;

  void setPageLayoutParams(const PageLayoutParams& pageLayoutParams);

  const OutputParams& getOutputParams() const;

  void setOutputParams(const OutputParams& outputParams);

  Units getUnits() const;

  void setUnits(Units units);

  /**
   * The unit in which a profile in \p profileUnits is shown when the program is set to
   * \p programUnits. Lengths in pixels depend on the resolution of a page, so a profile in pixels
   * stays in pixels, and with the program set to pixels a profile keeps its own unit.
   */
  static Units displayUnits(Units profileUnits, Units programUnits);

  /** This profile with its lengths in \p units. Neither its units nor \p units may be pixels, unless equal. */
  DefaultParams inUnits(Units units) const;


  /** The settings that apply to a whole project; new projects start with them. */
  const ProcessingOptions& getProcessingOptions() const { return m_processingOptions; }

  void setProcessingOptions(const ProcessingOptions& options) { m_processingOptions = options; }

 private:
  FixOrientationParams m_fixOrientationParams;
  DeskewParams m_deskewParams;
  PageSplitParams m_pageSplitParams;
  SelectContentParams m_selectContentParams;
  PageLayoutParams m_pageLayoutParams;
  OutputParams m_outputParams;
  Units m_units;
  ProcessingOptions m_processingOptions;
};


inline Units DefaultParams::getUnits() const {
  return m_units;
}

inline void DefaultParams::setUnits(Units units) {
  DefaultParams::m_units = units;
}

inline const DefaultParams::FixOrientationParams& DefaultParams::getFixOrientationParams() const {
  return m_fixOrientationParams;
}

inline void DefaultParams::setFixOrientationParams(const DefaultParams::FixOrientationParams& fixOrientationParams) {
  DefaultParams::m_fixOrientationParams = fixOrientationParams;
}

inline const DefaultParams::DeskewParams& DefaultParams::getDeskewParams() const {
  return m_deskewParams;
}

inline void DefaultParams::setDeskewParams(const DefaultParams::DeskewParams& deskewParams) {
  DefaultParams::m_deskewParams = deskewParams;
}

inline const DefaultParams::PageSplitParams& DefaultParams::getPageSplitParams() const {
  return m_pageSplitParams;
}

inline void DefaultParams::setPageSplitParams(const DefaultParams::PageSplitParams& pageSplitParams) {
  DefaultParams::m_pageSplitParams = pageSplitParams;
}

inline const DefaultParams::SelectContentParams& DefaultParams::getSelectContentParams() const {
  return m_selectContentParams;
}

inline void DefaultParams::setSelectContentParams(const DefaultParams::SelectContentParams& selectContentParams) {
  DefaultParams::m_selectContentParams = selectContentParams;
}

inline const DefaultParams::PageLayoutParams& DefaultParams::getPageLayoutParams() const {
  return m_pageLayoutParams;
}

inline void DefaultParams::setPageLayoutParams(const DefaultParams::PageLayoutParams& pageLayoutParams) {
  DefaultParams::m_pageLayoutParams = pageLayoutParams;
}

inline const DefaultParams::OutputParams& DefaultParams::getOutputParams() const {
  return m_outputParams;
}

inline void DefaultParams::setOutputParams(const DefaultParams::OutputParams& outputParams) {
  DefaultParams::m_outputParams = outputParams;
}





inline const Margins& DefaultParams::PageLayoutParams::getHardMargins() const {
  return m_hardMargins;
}

inline void DefaultParams::PageLayoutParams::setHardMargins(const Margins& hardMargins) {
  PageLayoutParams::m_hardMargins = hardMargins;
}

inline const page_layout::Alignment& DefaultParams::PageLayoutParams::getAlignment() const {
  return m_alignment;
}

inline void DefaultParams::PageLayoutParams::setAlignment(const page_layout::Alignment& alignment) {
  PageLayoutParams::m_alignment = alignment;
}

inline bool DefaultParams::PageLayoutParams::isAutoMargins() const {
  return m_autoMargins;
}

inline void DefaultParams::PageLayoutParams::setAutoMargins(bool autoMargins) {
  PageLayoutParams::m_autoMargins = autoMargins;
}

inline const Dpi& DefaultParams::OutputParams::getDpi() const {
  return m_dpi;
}

inline void DefaultParams::OutputParams::setDpi(const Dpi& dpi) {
  OutputParams::m_dpi = dpi;
}

inline const output::ColorParams& DefaultParams::OutputParams::getColorParams() const {
  return m_colorParams;
}

inline void DefaultParams::OutputParams::setColorParams(const output::ColorParams& colorParams) {
  OutputParams::m_colorParams = colorParams;
}

inline const output::SplittingOptions& DefaultParams::OutputParams::getSplittingOptions() const {
  return m_splittingOptions;
}

inline void DefaultParams::OutputParams::setSplittingOptions(const output::SplittingOptions& splittingOptions) {
  OutputParams::m_splittingOptions = splittingOptions;
}

inline const output::PictureShapeOptions& DefaultParams::OutputParams::getPictureShapeOptions() const {
  return m_pictureShapeOptions;
}

inline void DefaultParams::OutputParams::setPictureShapeOptions(
    const output::PictureShapeOptions& pictureShapeOptions) {
  OutputParams::m_pictureShapeOptions = pictureShapeOptions;
}

inline const output::DepthPerception& DefaultParams::OutputParams::getDepthPerception() const {
  return m_depthPerception;
}

inline void DefaultParams::OutputParams::setDepthPerception(const output::DepthPerception& depthPerception) {
  OutputParams::m_depthPerception = depthPerception;
}

inline const output::DewarpingOptions& DefaultParams::OutputParams::getDewarpingOptions() const {
  return m_dewarpingOptions;
}

inline void DefaultParams::OutputParams::setDewarpingOptions(const output::DewarpingOptions& dewarpingOptions) {
  OutputParams::m_dewarpingOptions = dewarpingOptions;
}

inline double DefaultParams::OutputParams::getDespeckleLevel() const {
  return m_despeckleLevel;
}

inline void DefaultParams::OutputParams::setDespeckleLevel(double despeckleLevel) {
  OutputParams::m_despeckleLevel = despeckleLevel;
}


#endif  // SCANTAILOR_CORE_DEFAULTPARAMS_H_
