// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RelinkingSortingModel.h"

#include "RelinkingModel.h"

RelinkingSortingModel::RelinkingSortingModel(QObject* parent)
    : QSortFilterProxyModel(parent), m_showMissingOnly(false) {
  setDynamicSortFilter(true);
  sort(0);
}

void RelinkingSortingModel::setShowMissingOnly(const bool missingOnly) {
  if (m_showMissingOnly != missingOnly) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_showMissingOnly = missingOnly;
    endFilterChange();
#else
    m_showMissingOnly = missingOnly;
    invalidateFilter();
#endif
  }
}

void RelinkingSortingModel::setPinned(const QModelIndex& sourceIndex) {
  if (m_pinned == sourceIndex) {
    return;
  }
  if (m_showMissingOnly) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_pinned = sourceIndex;
    endFilterChange();
#else
    m_pinned = sourceIndex;
    invalidateFilter();
#endif
  } else {
    m_pinned = sourceIndex;
  }
}

bool RelinkingSortingModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  const QString leftPath(left.data(RelinkingModel::PathRole).toString());
  const QString rightPath(right.data(RelinkingModel::PathRole).toString());
  const int res = leftPath.compare(rightPath, Qt::CaseInsensitive);
  if (res != 0) {
    return res < 0;
  }
  return leftPath < rightPath;
}

bool RelinkingSortingModel::filterAcceptsRow(const int sourceRow, const QModelIndex& sourceParent) const {
  if (!m_showMissingOnly) {
    return true;
  }
  const QModelIndex index(sourceModel()->index(sourceRow, 0, sourceParent));
  if (m_pinned.isValid() && ((index == m_pinned) || (index == m_pinned.parent()))) {
    return true;
  }
  return index.data(RelinkingModel::HasMissingRole).toBool();
}
