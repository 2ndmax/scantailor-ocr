// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLICATION_H_
#define SCANTAILOR_CORE_APPLICATION_H_

#include <QApplication>
#include <QTimer>

#include "Translations.h"

class Application : public QApplication {
  Q_OBJECT
 public:
  Application(int& argc, char** argv);

  bool notify(QObject* receiver, QEvent* e) override;

  /** Turns long plain text tooltips into rich text, which Qt wraps instead of showing one long line. */
  bool eventFilter(QObject* watched, QEvent* event) override;

  /**
   * A long plain text tooltip as rich text that is wrapped at about 120 characters; other tooltips
   * unchanged. For tooltips that aren't set on a widget, such as those of list items.
   */
  static QString wrapToolTip(const QString& tip);

  /**
   * The widget that gets a wheel event instead of \p receiver, or nullptr if \p receiver keeps it.
   * A list, number field or slider changes by the wheel only after it was clicked; otherwise the
   * wheel scrolls what holds it, so that scrolling a panel doesn't change settings on the way.
   */
  static QWidget* wheelReceiverInsteadOf(QObject* receiver);

  const QString& getCurrentLocale() const;

  void installLanguage(const QString& locale);

  std::list<QString> getLanguagesList() const;

 private:
  /** Tells the user about an unexpected error that was caught in notify(). */
  void showUnexpectedError(const QString& reason);

  Translations m_translations;
};


#endif
