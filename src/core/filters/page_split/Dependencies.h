// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_DEPENDENCIES_H_
#define SCANTAILOR_PAGE_SPLIT_DEPENDENCIES_H_

#include <QPolygonF>
#include <QSize>

#include "LayoutType.h"
#include "OrthogonalRotation.h"

class QString;
class QDomDocument;
class QDomElement;

namespace page_split {
class Params;

/**
 * \brief Dependencies of a page parameters.
 *
 * Once dependencies change, the stored page parameters are no longer valid.
 */
class Dependencies {
  // Member-wise copying is OK.
 public:
  Dependencies();

  explicit Dependencies(const QDomElement& el);

  /**
   * \param origPreCropArea The area left by the trim of the "Fix Orientation" step, in
   *        original image coordinates (ImageTransformation::origPreCropArea()). The split
   *        line is stored in coordinates relative to it.
   */
  Dependencies(const QSize& imageSize,
               OrthogonalRotation rotation,
               const QPolygonF& origPreCropArea,
               LayoutType layoutType);

  void setLayoutType(LayoutType type);

  const OrthogonalRotation& orientation() const;

  /**
   * \brief False for dependencies read from a project file written before the
   *        trim was part of them.
   */
  bool hasPreCropArea() const;

  const QPolygonF& preCropArea() const;

  void setPreCropArea(const QPolygonF& origPreCropArea);

  bool compatibleWith(const Params& params) const;

  bool isNull() const;

  QDomElement toXml(QDomDocument& doc, const QString& tagName) const;

 private:
  QSize m_imageSize;
  OrthogonalRotation m_rotation;
  // Empty if unknown, see hasPreCropArea().
  QPolygonF m_preCropArea;
  LayoutType m_layoutType;
};


inline const OrthogonalRotation& Dependencies::orientation() const {
  return m_rotation;
}

inline void Dependencies::setLayoutType(LayoutType type) {
  m_layoutType = type;
}

inline bool Dependencies::hasPreCropArea() const {
  return !m_preCropArea.empty();
}

inline const QPolygonF& Dependencies::preCropArea() const {
  return m_preCropArea;
}

inline void Dependencies::setPreCropArea(const QPolygonF& origPreCropArea) {
  m_preCropArea = origPreCropArea;
}
}  // namespace page_split
#endif  // ifndef SCANTAILOR_PAGE_SPLIT_DEPENDENCIES_H_
