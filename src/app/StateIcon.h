// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_STATEICON_H_
#define SCANTAILOR_APP_STATEICON_H_

#include <QPixmap>

/**
 * A small round icon in traffic light colors that tells the state of a tree item,
 * as used by the Relinking and Fix DPI dialogs.
 */
class StateIcon {
 public:
  /** None gives an empty icon of the same size, which keeps the text in place. */
  enum Color { Green, Yellow, Red, None };

  static QPixmap pixmap(Color color);

 private:
  static QPixmap create(Color color);
};


#endif  // ifndef SCANTAILOR_APP_STATEICON_H_
