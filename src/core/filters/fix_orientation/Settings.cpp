// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include <QDomElement>

#include "CoreUtils.h"
#include "ProjectIdReader.h"
#include "RelinkedKeys.h"

using namespace core;

namespace fix_orientation {
Settings::Settings() = default;

Settings::~Settings() = default;

void Settings::clear() {
  QMutexLocker locker(&m_mutex);
  clearLocked();
}

void Settings::clearLocked() {
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
  applyRotationLocked(imageId, rotation);
}

void Settings::applyRotation(const std::set<PageId>& pages, const OrthogonalRotation rotation) {
  QMutexLocker locker(&m_mutex);

  for (const PageId& page : pages) {
    applyRotationLocked(page.imageId(), rotation);
  }
}

void Settings::applyRotationLocked(const ImageId& imageId, const OrthogonalRotation rotation) {
  CoreUtils::mapSetValue(m_perImageRotation, imageId, rotation);
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
  setTrimLocked(imageId, trim);
}

void Settings::setTrimLocked(const ImageId& imageId, const ImageTrim& trim) {
  if (!trim.enabled) {
    m_perImageTrim.erase(imageId);
    return;
  }
  CoreUtils::mapSetValue(m_perImageTrim, imageId, trim);
}

ImageTrim Settings::getTrim(const ImageId& imageId) const {
  QMutexLocker locker(&m_mutex);
  const auto it(m_perImageTrim.find(imageId));
  if (it != m_perImageTrim.end()) {
    return it->second;
  }
  return ImageTrim();
}

void Settings::load(const ProjectIdReader& reader, const QDomElement& filterEl) {
  QMutexLocker locker(&m_mutex);
  clearLocked();

  const QString imageTagName("image");
  QDomNode node(filterEl.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != imageTagName) {
      continue;
    }
    QDomElement el(node.toElement());

    bool ok = true;
    const int id = el.attribute("id").toInt(&ok);
    if (!ok) {
      continue;
    }

    const ImageId imageId(reader.imageId(id));
    if (imageId.isNull()) {
      continue;
    }

    const QDomElement rotationEl(el.namedItem("rotation").toElement());
    if (!rotationEl.isNull()) {
      const OrthogonalRotation rotation(rotationEl);
      applyRotationLocked(imageId, rotation);
    }

    const QDomElement trimEl(el.namedItem("trim").toElement());
    if (!trimEl.isNull()) {
      ImageTrim trim;
      trim.enabled = (trimEl.attribute("enabled", "0") != "0");
      trim.left = trimEl.attribute("left", "0").toInt();
      trim.right = trimEl.attribute("right", "0").toInt();
      trim.top = trimEl.attribute("top", "0").toInt();
      trim.bottom = trimEl.attribute("bottom", "0").toInt();
      setTrimLocked(imageId, trim);
    }
  }
}  // Settings::load
}  // namespace fix_orientation
