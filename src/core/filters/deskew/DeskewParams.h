// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_DESKEWPARAMS_H_
#define SCANTAILOR_DESKEW_DESKEWPARAMS_H_

#include <AutoManualMode.h>

#include <QDomDocument>
#include <QDomElement>

#include "SkewDetection.h"

namespace deskew {
class DeskewParams {
 public:
  DeskewParams();

  explicit DeskewParams(double deskewAngleDeg, AutoManualMode mode, bool autoOblique = false);

  explicit DeskewParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  double getDeskewAngleDeg() const;

  void setDeskewAngleDeg(double deskewAngleDeg);

  AutoManualMode getMode() const;

  void setMode(AutoManualMode mode);

  bool isAutoOblique() const;

  void setAutoOblique(bool autoOblique);

  SkewDetection getDetection() const;

  void setDetection(SkewDetection detection);

 private:
  double m_deskewAngleDeg;
  AutoManualMode m_mode;
  bool m_autoOblique;
  SkewDetection m_detection;
};
}  // namespace deskew

#endif  // ifndef SCANTAILOR_DESKEW_DESKEWPARAMS_H_