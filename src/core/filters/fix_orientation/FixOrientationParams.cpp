// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FixOrientationParams.h"

#include <OrthogonalRotation.h>

fix_orientation::FixOrientationParams::FixOrientationParams(const OrthogonalRotation& imageRotation)
    : m_imageRotation(imageRotation) {}

fix_orientation::FixOrientationParams::FixOrientationParams(const QDomElement& el)
    : m_imageRotation(el.namedItem("imageRotation").toElement()) {}

QDomElement fix_orientation::FixOrientationParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_imageRotation.toXml(doc, "imageRotation"));
  return el;
}

const OrthogonalRotation& fix_orientation::FixOrientationParams::getImageRotation() const {
  return m_imageRotation;
}

void fix_orientation::FixOrientationParams::setImageRotation(const OrthogonalRotation& imageRotation) {
  m_imageRotation = imageRotation;
}