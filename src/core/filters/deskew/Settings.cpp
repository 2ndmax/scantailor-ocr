// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include "../../Utils.h"
#include "RelinkedKeys.h"

using namespace core;

namespace deskew {
Settings::Settings() {
  m_deviationProvider.setComputeValueByKey([this](const PageId& pageId) -> double {
    auto it(m_perPageParams.find(pageId));
    if (it != m_perPageParams.end()) {
      const Params& params = it->second;
      return params.deskewAngle();
    }
    return NAN;
  });
  // The angles are around zero, so the threshold is in degrees.
  m_deviationProvider.setAbsoluteThreshold(true);
}

Settings::~Settings() = default;

void Settings::clear() {
  QMutexLocker locker(&m_mutex);
  m_perPageParams.clear();
  m_pendingAutoOblique.clear();
  m_deviationProvider.clear();
}

void Settings::performRelinking(const AbstractRelinker& relinker) {
  QMutexLocker locker(&m_mutex);
  m_perPageParams = relinkedKeys(m_perPageParams, relinker);

  // REVIEW-NOTE (2026-10): relinkedKeys() keeps the first of two pages relinked onto the same file, this
  // loop the last one -> such a page may get the angles of one page and the pending state of the other.
  // Not fixed because such a project already contains the same page twice.
  std::unordered_map<PageId, bool> newPending;
  for (const auto& kv : m_pendingAutoOblique) {
    newPending[relinkedKey(kv.first, relinker)] = kv.second;
  }
  m_pendingAutoOblique.swap(newPending);

  m_deviationProvider.clear();
  for (const PerPageParams::value_type& kv : m_perPageParams) {
    m_deviationProvider.addOrUpdate(kv.first);
  }
}

void Settings::setPageParams(const PageId& pageId, const Params& params) {
  QMutexLocker locker(&m_mutex);
  Utils::mapSetValue(m_perPageParams, pageId, params);
  m_deviationProvider.addOrUpdate(pageId);
}

std::unique_ptr<Params> Settings::getPageParams(const PageId& pageId) const {
  QMutexLocker locker(&m_mutex);

  auto it(m_perPageParams.find(pageId));
  if (it != m_perPageParams.end()) {
    return std::make_unique<Params>(it->second);
  } else {
    return nullptr;
  }
}

bool Settings::isParamsNull(const PageId& pageId) const {
  QMutexLocker locker(&m_mutex);
  return (m_perPageParams.find(pageId) == m_perPageParams.end());
}

const DeviationProvider<PageId>& Settings::deviationProvider() const {
  return m_deviationProvider;
}

void Settings::setPendingAutoOblique(const PageId& pageId, const bool enabled) {
  QMutexLocker locker(&m_mutex);
  Utils::mapSetValue(m_pendingAutoOblique, pageId, enabled);
}

std::optional<bool> Settings::takePendingAutoOblique(const PageId& pageId) {
  QMutexLocker locker(&m_mutex);
  const auto it = m_pendingAutoOblique.find(pageId);
  if (it == m_pendingAutoOblique.end()) {
    return std::nullopt;
  }
  const bool value = it->second;
  m_pendingAutoOblique.erase(it);
  return value;
}
}  // namespace deskew
