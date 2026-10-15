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
    m_showMissingOnly = missingOnly;
    invalidateFilter();
  }
}

bool RelinkingSortingModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  const QString leftPath(left.data(RelinkingModel::UncommittedPathRole).toString());
  const QString rightPath(right.data(RelinkingModel::UncommittedPathRole).toString());
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
  return sourceModel()->index(sourceRow, 0, sourceParent).data(RelinkingModel::HasMissingRole).toBool();
}
