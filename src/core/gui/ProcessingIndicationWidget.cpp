// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProcessingIndicationWidget.h"

#include <ColorInterpolation.h>

#include <QHideEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QTimerEvent>

#include "ColorSchemeManager.h"

using namespace imageproc;

static const double distinctionIncrease = 1.0 / 5.0;
static const double distinctionDecrease = -1.0 / 3.0;

ProcessingIndicationWidget::ProcessingIndicationWidget(QWidget* parent)
    : QWidget(parent),
      m_animation(10),
      m_headColor(ColorSchemeManager::instance().getColorParam("ProcessingIndicationHead")),
      m_tailColor(ColorSchemeManager::instance().getColorParam("ProcessingIndicationTail")),
      m_distinction(1.0),
      m_distinctionDelta(distinctionIncrease),
      m_timerId(0) {}

void ProcessingIndicationWidget::resetAnimation() {
  m_distinction = 1.0;
  m_distinctionDelta = distinctionIncrease;
}

void ProcessingIndicationWidget::processingRestartedEffect() {
  m_distinction = 1.0;
  m_distinctionDelta = distinctionDecrease;
}

void ProcessingIndicationWidget::paintEvent(QPaintEvent* event) {
  QPainter painter(this);

  QColor fadeColor = ColorSchemeManager::instance().getColorParam("ProcessingIndicationFade");
  fadeColor.setAlpha(127);
  painter.fillRect(event->rect(), fadeColor);

  const QRect animationRect(this->animationRect());
  if (!animationRect.isEmpty() && event->rect().intersects(animationRect)) {
    const QColor headColor(colorInterpolation(m_tailColor, m_headColor, m_distinction));

    m_distinction += m_distinctionDelta;
    if (m_distinction > 1.0) {
      m_distinction = 1.0;
    } else if (m_distinction <= 0.0) {
      m_distinction = 0.0;
      m_distinctionDelta = distinctionIncrease;
    }

    m_animation.nextFrame(headColor, m_tailColor, &painter, animationRect);

    if (m_timerId == 0) {
      m_timerId = startTimer(180);
    }
  }
}

void ProcessingIndicationWidget::timerEvent(QTimerEvent* event) {
  killTimer(event->timerId());
  m_timerId = 0;
  update(animationRect());
}

void ProcessingIndicationWidget::hideEvent(QHideEvent* event) {
  if (m_timerId != 0) {
    killTimer(m_timerId);
    m_timerId = 0;
  }
  QWidget::hideEvent(event);
}

QRect ProcessingIndicationWidget::animationRect() const {
  QRect r(0, 0, 80, 80);
  r.moveCenter(rect().center());
  r &= rect();
  return r;
}
