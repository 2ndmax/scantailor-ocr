// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DEFAULTPARAMS_H_
#define SCANTAILOR_CORE_DEFAULTPARAMS_H_


#include "filters/deskew/DeskewParams.h"
#include <filters/deskew/SkewDetection.h>
#include "filters/output/DefaultOutputParams.h"
#include <filters/output/ColorParams.h>
#include <filters/output/DepthPerception.h>
#include <filters/output/DespeckleLevel.h>
#include <filters/output/DewarpingOptions.h>
#include <filters/output/PictureShapeOptions.h>
#include "filters/page_layout/PageLayoutParams.h"
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

  using PageLayoutParams = page_layout::PageLayoutParams;

  using OutputParams = output::DefaultOutputParams;

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






#endif  // SCANTAILOR_CORE_DEFAULTPARAMS_H_
