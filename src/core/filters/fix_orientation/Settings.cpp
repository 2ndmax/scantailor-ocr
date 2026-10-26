// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include "../../Utils.h"
#include "RelinkedKeys.h"

using namespace core;

namespace fix_orientation {
Settings::Settings() = default;

Settings::~Settings() = default;

void Settings::clear() {
  QMutexLocker locker(&m_mutex);
  m_perImageRotation.clear();
  m_perImageTrim.clear();
}

void Settings::performRelinking(const AbstractRelinker& relinker) {
  QMutexLocker locker(&m_mutex);
  m_perImageRotation = relinkedKeys(m_perImageRotation, relinker);
  m_perImageTrim = relinkedKeys(m_perImageTrim, relinker);
}

void Settings::applyRotation(const ImageId& imageId, const OrthogonalRotation rotation) {
  QMutexLocker locker(&m_mutex);
  Utils::mapSetValue(m_perImageRotation, imageId, rotation);
}

void Settings::applyRotation(const std::set<PageId>& pages, const OrthogonalRotation rotation) {
  QMutexLocker locker(&m_mutex);

  for (const PageId& page : pages) {
    Utils::mapSetValue(m_perImageRotation, page.imageId(), rotation);
  }
}

OrthogonalRotation Settings::getRotationFor(const ImageId& imageId) const {
  QMutexLocker locker(&m_mutex);

  auto it(m_perImageRotation.find(imageId));
  if (it != m_perImageRotation.end()) {
    return it->second;
  } else {
    return OrthogonalRotation();
  }
}

bool Settings::isRotationNull(const ImageId& imageId) const {
  QMutexLocker locker(&m_mutex);
  return (m_perImageRotation.find(imageId) == m_perImageRotation.end());
}

void Settings::setTrim(const ImageId& imageId, const ImageTrim& trim) {
  QMutexLocker locker(&m_mutex);
  if (!trim.enabled) {
    m_perImageTrim.erase(imageId);
    return;
  }
  Utils::mapSetValue(m_perImageTrim, imageId, trim);
}

ImageTrim Settings::getTrim(const ImageId& imageId) const {
  QMutexLocker locker(&m_mutex);
  const auto it(m_perImageTrim.find(imageId));
  if (it != m_perImageTrim.end()) {
    return it->second;
  }
  return ImageTrim();
}
}  // namespace fix_orientation