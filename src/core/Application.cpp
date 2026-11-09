// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Application.h"

#include <QAbstractSpinBox>
#include <QComboBox>
#include <QDebug>
#include <QFontMetrics>
#include <QLineEdit>
#include <QMessageBox>
#include <QSlider>
#include <QTextDocument>
#include <QTimer>
#include <QToolTip>
#include <QWidget>
#include <atomic>
#include <exception>

#include "OutOfMemoryHandler.h"

Application::Application(int& argc, char** argv) : QApplication(argc, argv) {
  installEventFilter(this);
}

bool Application::eventFilter(QObject* watched, QEvent* event) {
  if (event->type() == QEvent::ToolTipChange && watched->isWidgetType()) {
    auto* widget = static_cast<QWidget*>(watched);
    const QString tip = widget->toolTip();
    // Setting the rich text version triggers this event again, but then the text is rich text
    // already and stays as it is.
    const QString wrapped = wrapToolTip(tip);
    if (wrapped != tip) {
      widget->setToolTip(wrapped);
    }
  }
  return QApplication::eventFilter(watched, event);
}

QString Application::wrapToolTip(const QString& tip) {
  // Tips up to this many characters stay on one line, longer ones are wrapped at about this
  // width. Qt itself wraps rich text at a fixed 80 characters, so the width is given by a table.
  const int wrapLength = 120;
  if (tip.size() <= wrapLength || Qt::mightBeRichText(tip)) {
    return tip;
  }
  const int width = QFontMetrics(QToolTip::font()).averageCharWidth() * wrapLength;
  return QString("<table width=\"%1\" cellspacing=\"0\" cellpadding=\"0\"><tr><td>%2</td></tr></table>")
      .arg(width)
      .arg(tip.toHtmlEscaped().replace('\n', "<br>"));
}

QWidget* Application::wheelReceiverInsteadOf(QObject* receiver) {
  auto* widget = qobject_cast<QWidget*>(receiver);
  if (!widget) {
    return nullptr;
  }
  // The wheel reaches the line edit inside an editable list or a number field first.
  QWidget* control = qobject_cast<QLineEdit*>(widget) ? widget->parentWidget() : widget;
  const bool changesByWheel = qobject_cast<QComboBox*>(control) || qobject_cast<QAbstractSpinBox*>(control)
                              || qobject_cast<QSlider*>(control);
  if (!changesByWheel || control->hasFocus()) {
    return nullptr;
  }
  return control->parentWidget();
}

bool Application::notify(QObject* receiver, QEvent* e) {
  try {
    // Has to be decided here: inside QApplication::notify() the wheel would first give the control the focus.
    if (e->type() == QEvent::Wheel) {
      if (QWidget* insteadOfReceiver = wheelReceiverInsteadOf(receiver)) {
        return QApplication::notify(insteadOfReceiver, e);
      }
    }
    return QApplication::notify(receiver, e);
  } catch (const std::bad_alloc&) {
    OutOfMemoryHandler::instance().handleOutOfMemorySituation();
    return false;
  } catch (const std::exception& ex) {
    // An exception escaping an event handler would otherwise terminate the program silently.
    qCritical().noquote() << "Unexpected error while handling an event:" << ex.what();
    showUnexpectedError(QString::fromUtf8(ex.what()));
    return false;
  } catch (...) {
    // Not derived from std::exception, so there is no reason to show.
    qCritical().noquote() << "Unexpected error while handling an event.";
    showUnexpectedError(QString());
    return false;
  }
}

void Application::showUnexpectedError(const QString& reason) {
  // Shown with a delay, not from within the failing event handler, and only once
  // for a burst of errors. notify() also runs in worker threads that have an event
  // loop, hence the atomic flag and the timer, which runs the lambda in the GUI thread.
  static std::atomic<bool> messagePending(false);
  if (messagePending.exchange(true)) {
    return;
  }
  QTimer::singleShot(0, this, [reason]() {
    QMessageBox::critical(activeWindow(), tr("Unexpected Error"),
                          tr("An unexpected error occurred:\n%1\n\n"
                             "ScanTailor OCR continues to run, but to be safe, save your project under "
                             "a new name and restart the program.")
                              .arg(reason));
    messagePending = false;
  });
}


void Application::installLanguage(const QString& locale) {
  m_translations.install(locale);
}

const QString& Application::getCurrentLocale() const {
  return m_translations.currentLocale();
}

std::list<QString> Application::getLanguagesList() const {
  return m_translations.languages();
}
