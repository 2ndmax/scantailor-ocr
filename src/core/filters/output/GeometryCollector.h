// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_GEOMETRYCOLLECTOR_H_
#define SCANTAILOR_OUTPUT_GEOMETRYCOLLECTOR_H_

#include <QPolygonF>
#include <optional>

#include "AbstractFilterDataCollector.h"
#include "ImageTransformation.h"

namespace output {
/**
 * Collects the geometry of a page as the Output step would use it (the transformation and the
 * content area from the steps before), without processing any image. Passed through the chain of
 * cache driven tasks; stays empty if a step before has not been done for the page yet.
 */
class GeometryCollector : public AbstractFilterDataCollector {
 public:
  void setGeometry(const ImageTransformation& xform, const QPolygonF& contentRectPhys) {
    m_xform = xform;
    m_contentRectPhys = contentRectPhys;
  }

  bool hasGeometry() const { return m_xform.has_value(); }

  const ImageTransformation& xform() const { return *m_xform; }

  const QPolygonF& contentRectPhys() const { return m_contentRectPhys; }

 private:
  std::optional<ImageTransformation> m_xform;
  QPolygonF m_contentRectPhys;
};
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_GEOMETRYCOLLECTOR_H_
