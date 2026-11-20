// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_FIXORIENTATIONPARAMS_H_
#define SCANTAILOR_FIX_ORIENTATION_FIXORIENTATIONPARAMS_H_

#include <OrthogonalRotation.h>

#include <QDomDocument>
#include <QDomElement>

namespace fix_orientation {
class FixOrientationParams {
 public:
  FixOrientationParams() = default;

  explicit FixOrientationParams(const OrthogonalRotation& imageRotation);

  explicit FixOrientationParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  const OrthogonalRotation& getImageRotation() const;

  void setImageRotation(const OrthogonalRotation& imageRotation);

 private:
  OrthogonalRotation m_imageRotation;
};
}  // namespace fix_orientation

#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_FIXORIENTATIONPARAMS_H_