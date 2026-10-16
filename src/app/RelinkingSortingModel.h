// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_RELINKINGSORTINGMODEL_H_
#define SCANTAILOR_APP_RELINKINGSORTINGMODEL_H_

#include <QSortFilterProxyModel>

class RelinkingSortingModel : public QSortFilterProxyModel {
 public:
  explicit RelinkingSortingModel(QObject* parent = nullptr);

  /** If set, only folders with missing files and only the missing files themselves are shown. */
  void setShowMissingOnly(bool missingOnly);

  /**
   * Keeps \p sourceIndex (and its folder) visible even if nothing is missing there,
   * so a node doesn't vanish right after being relinked.
   */
  void setPinned(const QModelIndex& sourceIndex);

 protected:
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;

  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

 private:
  bool m_showMissingOnly;
  QPersistentModelIndex m_pinned;
};


#endif
