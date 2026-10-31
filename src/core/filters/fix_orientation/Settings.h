// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_SETTINGS_H_
#define SCANTAILOR_FIX_ORIENTATION_SETTINGS_H_

#include <QMutex>
#include <set>
#include <unordered_map>

#include "ImageId.h"
#include "ImageTrim.h"
#include "NonCopyable.h"
#include "OrthogonalRotation.h"
#include "PageId.h"

class AbstractRelinker;
class ProjectReader;
class QDomElement;

namespace fix_orientation {
class Settings {
  DECLARE_NON_COPYABLE(Settings)

 public:
  Settings();

  virtual ~Settings();

  void clear();

  /**
   * Replaces the settings with those of the project file's element of this filter.
   */
  void load(const ProjectReader& reader, const QDomElement& filterEl);

  void performRelinking(const AbstractRelinker& relinker);

  void applyRotation(const ImageId& imageId, OrthogonalRotation rotation);

  void applyRotation(const std::set<PageId>& pages, OrthogonalRotation rotation);

  OrthogonalRotation getRotationFor(const ImageId& imageId) const;

  bool isRotationNull(const ImageId& imageId) const;

  void setTrim(const ImageId& imageId, const ImageTrim& trim);

  ImageTrim getTrim(const ImageId& imageId) const;

 private:
  using PerImageRotation = std::unordered_map<ImageId, OrthogonalRotation>;
  using PerImageTrim = std::unordered_map<ImageId, ImageTrim>;

  mutable QMutex m_mutex;
  PerImageRotation m_perImageRotation;
  PerImageTrim m_perImageTrim;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_SETTINGS_H_
