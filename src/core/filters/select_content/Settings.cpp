// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include <iostream>

#include "../../Utils.h"
#include "RelinkedKeys.h"

using namespace core;

namespace select_content {
Settings::Settings() : m_pageDetectionBox(0.0, 0.0), m_pageDetectionTolerance(0.1) {
  m_deviationProvider.setComputeValueByKey([this](const PageId& pageId) -> double {
    auto it(m_pageParams.find(pageId));
    if (it != m_pageParams.end()) {
      const Params& params = it->second;
      const QSizeF& contentSizeMM = params.contentSizeMM();
      if (!contentSizeMM.toSize().isEmpty())
        return std::sqrt(std::pow(contentSizeMM.width(), 2) + std::pow(contentSizeMM.height(), 2));
    }
    return NAN;
  });
}

Settings::~Settings() = default;

void Settings::clear() {
  QMutexLocker locker(&m_mutex);
  m_pageParams.clear();
  m_deviationProvider.clear();
}

void Settings::performRelinking(const AbstractRelinker& relinker) {
  QMutexLocker locker(&m_mutex);
  m_pageParams = relinkedKeys(m_pageParams, relinker);

  m_deviationProvider.clear();
  for (const PageParams::value_type& kv : m_pageParams) {
    m_deviationProvider.addOrUpdate(kv.first);
  }
}

void Settings::setPageParams(const PageId& pageId, const Params& params) {
  QMutexLocker locker(&m_mutex);
  Utils::mapSetValue(m_pageParams, pageId, params);
  m_deviationProvider.addOrUpdate(pageId);
}

void Settings::clearPageParams(const PageId& pageId) {
  QMutexLocker locker(&m_mutex);
  m_pageParams.erase(pageId);
  m_deviationProvider.remove(pageId);
}

std::unique_ptr<Params> Settings::getPageParams(const PageId& pageId) const {
  QMutexLocker locker(&m_mutex);

  const auto it(m_pageParams.find(pageId));
  if (it != m_pageParams.end()) {
    return std::make_unique<Params>(it->second);
  } else {
    return nullptr;
  }
}

bool Settings::isParamsNull(const PageId& pageId) const {
  QMutexLocker locker(&m_mutex);
  return (m_pageParams.find(pageId) == m_pageParams.end());
}

// The detection box and tolerance are read by processing threads while loading a project
// writes them, so they are guarded like the page params.
QSizeF Settings::pageDetectionBox() const {
  QMutexLocker locker(&m_mutex);
  return m_pageDetectionBox;
}

void Settings::setPageDetectionBox(QSizeF size) {
  QMutexLocker locker(&m_mutex);
  m_pageDetectionBox = size;
}

double Settings::pageDetectionTolerance() const {
  QMutexLocker locker(&m_mutex);
  return m_pageDetectionTolerance;
}

void Settings::setPageDetectionTolerance(double tolerance) {
  QMutexLocker locker(&m_mutex);
  m_pageDetectionTolerance = tolerance;
}

const DeviationProvider<PageId>& Settings::deviationProvider() const {
  return m_deviationProvider;
}

}  // namespace select_content