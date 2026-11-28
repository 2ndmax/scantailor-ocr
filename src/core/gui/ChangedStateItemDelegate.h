// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_CHANGEDSTATEITEMDELEGATE_H_
#define SCANTAILOR_CORE_CHANGEDSTATEITEMDELEGATE_H_

#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>

/**
 * \brief A decoration of an existing item delegate
 *        that forces certain item states.
 */
template <typename T = QStyledItemDelegate>
class ChangedStateItemDelegate : public T {
 public:
  explicit ChangedStateItemDelegate(QObject* parent = nullptr) : T(parent), m_changedFlags(), m_changedMask() {}

  void flagsForceEnabled(QStyle::State flags) {
    m_changedFlags |= flags;
    m_changedMask |= flags;
  }

  void flagsForceDisabled(QStyle::State flags) {
    m_changedFlags &= ~flags;
    m_changedMask |= flags;
  }

  void removeChanges(QStyle::State remove) { m_changedMask &= ~remove; }

  void removeAllChanges() { m_changedMask = QStyle::State(); }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    QStyleOptionViewItem modifiedOption(option);
    modifiedOption.state = (option.state & ~m_changedMask) | (m_changedFlags & m_changedMask);
    T::paint(painter, modifiedOption, index);
  }

 private:
  QStyle::State m_changedFlags;
  QStyle::State m_changedMask;
};


#endif  // ifndef SCANTAILOR_CORE_CHANGEDSTATEITEMDELEGATE_H_
