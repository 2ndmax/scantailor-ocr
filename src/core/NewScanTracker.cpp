// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "NewScanTracker.h"

void NewScanTracker::reset(const QSet<QString>& existing) {
  m_ignored = existing;
  m_pending.clear();
}

std::vector<QString> NewScanTracker::update(const std::vector<FileState>& listing, const qint64 nowMs) {
  QSet<QString> present;
  std::vector<QString> ready;
  for (const FileState& file : listing) {
    present.insert(file.path);
    if (m_ignored.contains(file.path)) {
      continue;
    }

    auto it = m_pending.find(file.path);
    if (it == m_pending.end()) {
      Pending pending;
      pending.size = file.size;
      pending.modified = file.modified;
      pending.firstSeenMs = nowMs;
      m_pending.emplace(file.path, pending);
      continue;
    }

    Pending& pending = it->second;
    if (pending.loading) {
      continue;
    }
    if ((file.size > 0) && (file.size == pending.size) && (file.modified == pending.modified)) {
      ++pending.stableChecks;
    } else {
      pending.size = file.size;
      pending.modified = file.modified;
      pending.stableChecks = 0;
    }
    if (pending.stableChecks >= STABLE_CHECKS) {
      pending.loading = true;
      ready.push_back(file.path);
    }
  }

  // A file that is gone, e.g. a temporary file renamed by the scanning program, is forgotten.
  for (auto it = m_pending.begin(); it != m_pending.end();) {
    if (!it->second.loading && !present.contains(it->first)) {
      it = m_pending.erase(it);
    } else {
      ++it;
    }
  }
  return ready;
}

void NewScanTracker::done(const QString& path) {
  m_pending.erase(path);
  m_ignored.insert(path);
}

bool NewScanTracker::loadFailed(const QString& path, const qint64 nowMs) {
  auto it = m_pending.find(path);
  if (it == m_pending.end()) {
    return true;
  }
  if (nowMs - it->second.firstSeenMs >= GIVE_UP_MS) {
    done(path);
    return true;
  }
  it->second.loading = false;
  it->second.stableChecks = 0;
  return false;
}
