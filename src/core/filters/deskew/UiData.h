// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_UIDATA_H_
#define SCANTAILOR_DESKEW_UIDATA_H_

#include "AutoManualMode.h"
#include "Dependencies.h"
#include "SkewDetection.h"

namespace deskew {
/**
 * What processing a page found, for the options panel to show.
 */
class UiData {
  // Member-wise copying is OK.
 public:
  UiData();

  ~UiData();

  void setEffectiveDeskewAngle(double degrees);

  double effectiveDeskewAngle() const;

  void setEffectiveObliqueAngle(double degrees);

  double effectiveObliqueAngle() const;

  void setDependencies(const Dependencies& deps);

  const Dependencies& dependencies() const;

  void setMode(AutoManualMode mode);

  AutoManualMode mode() const;

  void setObliqueMode(AutoManualMode mode);

  AutoManualMode obliqueMode() const;

  void setDetection(SkewDetection detection);

  SkewDetection detection() const;

 private:
  double m_effDeskewAngle;
  double m_effObliqueAngle;
  Dependencies m_deps;
  AutoManualMode m_mode;
  AutoManualMode m_obliqueMode;
  SkewDetection m_detection;
};

inline void UiData::setEffectiveDeskewAngle(const double degrees) {
  m_effDeskewAngle = degrees;
}

inline double UiData::effectiveDeskewAngle() const {
  return m_effDeskewAngle;
}

inline void UiData::setEffectiveObliqueAngle(const double degrees) {
  m_effObliqueAngle = degrees;
}

inline double UiData::effectiveObliqueAngle() const {
  return m_effObliqueAngle;
}

inline void UiData::setDependencies(const Dependencies& deps) {
  m_deps = deps;
}

inline const Dependencies& UiData::dependencies() const {
  return m_deps;
}

inline void UiData::setMode(const AutoManualMode mode) {
  m_mode = mode;
}

inline AutoManualMode UiData::mode() const {
  return m_mode;
}

inline void UiData::setObliqueMode(const AutoManualMode mode) {
  m_obliqueMode = mode;
}

inline AutoManualMode UiData::obliqueMode() const {
  return m_obliqueMode;
}

inline void UiData::setDetection(const SkewDetection detection) {
  m_detection = detection;
}

inline SkewDetection UiData::detection() const {
  return m_detection;
}
}  // namespace deskew
#endif  // SCANTAILOR_DESKEW_UIDATA_H_
