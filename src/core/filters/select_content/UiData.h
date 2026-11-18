// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_UIDATA_H_
#define SCANTAILOR_SELECT_CONTENT_UIDATA_H_

#include <AutoManualMode.h>
#include <QRectF>
#include <QSizeF>
#include <optional>

#include "Dependencies.h"
#include "DetectionOptions.h"
#include "PhysSizeCalc.h"

namespace select_content {
/**
 * What processing a page found, for the options panel to show.
 */
class UiData {
  // Member-wise copying is OK.
 public:
  UiData();

  ~UiData();

  void setSizeCalc(const PhysSizeCalc& calc);

  void setContentRect(const QRectF& contentRect);

  void setPageRect(const QRectF& contentRect);

  const QRectF& contentRect() const;

  const QRectF& pageRect() const;

  QSizeF contentSizeMM() const;

  void setDependencies(const Dependencies& deps);

  const Dependencies& dependencies() const;

  void setContentDetectionMode(AutoManualMode mode);

  void setPageDetectionMode(AutoManualMode mode);

  bool isFineTuningCornersEnabled() const;

  void setFineTuneCornersEnabled(bool fineTune);

  AutoManualMode contentDetectionMode() const;

  AutoManualMode pageDetectionMode() const;

  /** Those of the page, see Params::detectionOptions(). */
  const std::optional<DetectionOptions>& detectionOptions() const { return m_detectionOptions; }

  void setDetectionOptions(const std::optional<DetectionOptions>& options) { m_detectionOptions = options; }

 private:
  QRectF m_contentRect;  // In virtual image coordinates.
  QRectF m_pageRect;
  PhysSizeCalc m_sizeCalc;
  Dependencies m_deps;
  AutoManualMode m_contentDetectionMode;
  AutoManualMode m_pageDetectionMode;
  bool m_fineTuneCornersEnabled;
  std::optional<DetectionOptions> m_detectionOptions;
};

inline void UiData::setSizeCalc(const PhysSizeCalc& calc) {
  m_sizeCalc = calc;
}

inline void UiData::setContentRect(const QRectF& contentRect) {
  m_contentRect = contentRect;
}

inline const QRectF& UiData::contentRect() const {
  return m_contentRect;
}

inline void UiData::setPageRect(const QRectF& pageRect) {
  m_pageRect = pageRect;
}

inline const QRectF& UiData::pageRect() const {
  return m_pageRect;
}

inline QSizeF UiData::contentSizeMM() const {
  return m_sizeCalc.sizeMM(m_contentRect);
}

inline void UiData::setDependencies(const Dependencies& deps) {
  m_deps = deps;
}

inline const Dependencies& UiData::dependencies() const {
  return m_deps;
}

inline void UiData::setContentDetectionMode(AutoManualMode mode) {
  m_contentDetectionMode = mode;
}

inline AutoManualMode UiData::contentDetectionMode() const {
  return m_contentDetectionMode;
}

inline void UiData::setPageDetectionMode(AutoManualMode mode) {
  m_pageDetectionMode = mode;
}

inline AutoManualMode UiData::pageDetectionMode() const {
  return m_pageDetectionMode;
}

inline void UiData::setFineTuneCornersEnabled(bool fineTune) {
  m_fineTuneCornersEnabled = fineTune;
}

inline bool UiData::isFineTuningCornersEnabled() const {
  return m_fineTuneCornersEnabled;
}
}  // namespace select_content
#endif  // SCANTAILOR_SELECT_CONTENT_UIDATA_H_
