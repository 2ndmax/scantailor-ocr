// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoneCreationInteraction.h"

#include <QDebug>
#include <QKeyEvent>
#include <QPainter>
#include <QShortcut>
#include <functional>

#include "ImageViewBase.h"
#include "Zone.h"
#include "ZoneInteractionContext.h"

ZoneCreationInteraction::ZoneCreationInteraction(ZoneInteractionContext& context, InteractionState& interaction)
    : m_context(context),
      m_dragHandler(context.imageView(),
                    std::bind(&ZoneCreationInteraction::isDragHandlerPermitted, this, std::placeholders::_1)),
      m_dragWatcher(m_dragHandler),
      m_zoomHandler(context.imageView(), [](const InteractionState&) { return true; }),
      m_spline(std::make_shared<EditableSpline>()),
      m_initialZoneCreationMode(context.getZoneCreationMode()),
      m_leftMouseButtonPressed(m_initialZoneCreationMode == ZoneCreationMode::LASSO) {
  const QPointF screenMousePos(m_context.imageView().mapFromGlobal(QCursor::pos()) + QPointF(0.5, 0.5));
  const QTransform fromScreen(m_context.imageView().widgetToImage());
  m_nextVertexImagePos = fromScreen.map(screenMousePos);

  m_nextVertexImagePos_mid1 = m_nextVertexImagePos;
  m_nextVertexImagePos_mid2 = m_nextVertexImagePos;

  makeLastFollower(m_dragHandler);
  m_dragHandler.makeFirstFollower(m_dragWatcher);

  // REVIEW-NOTE (2026-10): while a zone is drawn, this handler and the one of the editor both own the +/- shortcuts
  // -> the keys are ambiguous and do not zoom while drawing (IA-02). Not fixed because using the editor's handler
  // needs a design change: the editor would have to allow zooming while this interaction holds the capture, or
  // the shortcuts would have to exist once per ImageView.
  makeLastFollower(m_zoomHandler);

  interaction.capture(m_interaction);
  m_spline->appendVertex(m_nextVertexImagePos);

  updateStatusTip();

  m_cancelShortcut = std::make_unique<QShortcut>(Qt::Key_Escape, &m_context.imageView());
  m_cancelShortcut->setAutoRepeat(false);
  QObject::connect(m_cancelShortcut.get(), &QShortcut::activated, &m_context.imageView(),
                   std::bind(&ZoneCreationInteraction::cancel, this));
}

void ZoneCreationInteraction::onPaint(QPainter& painter, const InteractionState& /*interaction*/) {
  painter.setWorldMatrixEnabled(false);
  painter.setRenderHint(QPainter::Antialiasing);

  const QTransform toScreen(m_context.imageView().imageToWidget());

  m_visualizer.drawSplines(painter, toScreen, m_context.zones());

  // Placed edges in the solid color, the edges that follow the mouse in the highlight color.
  const QPen solidLinePen(m_visualizer.activePen());
  QPen movingLinePen(solidLinePen);
  movingLinePen.setColor(m_visualizer.highlightDarkColor());

  painter.setBrush(Qt::NoBrush);

  if (currentZoneCreationMode() == ZoneCreationMode::RECTANGULAR) {
    if (m_nextVertexImagePos != m_spline->firstVertex()->point()) {
      m_visualizer.drawVertex(painter, toScreen.map(m_nextVertexImagePos_mid1), m_visualizer.highlightBrightColor());
      m_visualizer.drawVertex(painter, toScreen.map(m_nextVertexImagePos_mid2), m_visualizer.highlightBrightColor());

      // All four sides of the rectangle follow the mouse.
      const QPointF first(m_spline->firstVertex()->point());
      painter.setPen(movingLinePen);
      painter.drawLine(toScreen.map(QLineF(first, m_nextVertexImagePos_mid1)));
      painter.drawLine(toScreen.map(QLineF(m_nextVertexImagePos_mid1, m_nextVertexImagePos)));
      painter.drawLine(toScreen.map(QLineF(first, m_nextVertexImagePos_mid2)));
      painter.drawLine(toScreen.map(QLineF(m_nextVertexImagePos_mid2, m_nextVertexImagePos)));
    }
  } else {
    for (EditableSpline::SegmentIterator it(*m_spline); it.hasNext();) {
      const SplineSegment segment(it.next());
      // With the mouse back on the first vertex, the edge from it closes the zone and follows the mouse.
      const bool closingEdge
          = (segment.prev == m_spline->firstVertex()) && (segment.prev->point() == m_nextVertexImagePos);
      painter.setPen(closingEdge ? movingLinePen : solidLinePen);
      painter.drawLine(toScreen.map(segment.toLine()));
    }

    painter.setPen(movingLinePen);
    painter.drawLine(toScreen.map(QLineF(m_spline->lastVertex()->point(), m_nextVertexImagePos)));
  }

  m_visualizer.drawVertex(painter, toScreen.map(m_nextVertexImagePos), m_visualizer.highlightBrightColor());
}  // ZoneCreationInteraction::onPaint

void ZoneCreationInteraction::addFinishedZone() {
  m_spline->setBridged(true);
  // A zone without area (all points on one line, e.g. a rectangle of zero width) covers nothing,
  // and loading the project would drop it anyway (Zone::isValid()). So it is not added at all.
  if (!Zone(*m_spline).isValid()) {
    return;
  }
  m_context.zones().addZone(m_spline);
  m_context.zones().commit();
}

void ZoneCreationInteraction::cancel() {
  makePeerPreceeder(*m_context.createDefaultInteraction());
  m_context.imageView().update();
  delete this;
}

void ZoneCreationInteraction::onMousePressEvent(QMouseEvent* event, InteractionState& /*interaction*/) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  m_leftMouseButtonPressed = true;
  m_lassoVertexAddedSincePress = false;
  m_mouseButtonModifiers = event->modifiers();
  event->accept();
}

void ZoneCreationInteraction::onMouseReleaseEvent(QMouseEvent* event, InteractionState& interaction) {
  if (event->button() != Qt::LeftButton) {
    return;
  }
  m_leftMouseButtonPressed = false;

  ZoneCreationMode currentCreationMode = currentZoneCreationMode();
  if (m_dragWatcher.haveSignificantDrag()) {
    return;
  }

  const QTransform toScreen(m_context.imageView().imageToWidget());
  const QTransform fromScreen(m_context.imageView().widgetToImage());
  const QPointF screenMousePos(event->pos() + QPointF(0.5, 0.5));
  const QPointF imageMousePos(fromScreen.map(screenMousePos));

  if (currentCreationMode == ZoneCreationMode::RECTANGULAR) {
    if (m_nextVertexImagePos != m_spline->firstVertex()->point()) {
      QPointF firstPoint = m_spline->firstVertex()->point();

      m_spline.reset(new EditableSpline);

      m_spline->appendVertex(firstPoint);
      m_spline->appendVertex(m_nextVertexImagePos_mid1);
      m_spline->appendVertex(m_nextVertexImagePos);
      m_spline->appendVertex(m_nextVertexImagePos_mid2);

      addFinishedZone();
    }

    makePeerPreceeder(*m_context.createDefaultInteraction());
    m_context.imageView().update();
    delete this;
  } else {
    if (m_spline->hasAtLeastSegments(2) && (m_nextVertexImagePos == m_spline->firstVertex()->point())) {
      // Finishing the spline. Bridging the first and the last points
      // will create another segment.
      addFinishedZone();

      makePeerPreceeder(*m_context.createDefaultInteraction());
      m_context.imageView().update();
      delete this;
    } else if (!m_lassoVertexAddedSincePress && (m_nextVertexImagePos == m_spline->lastVertex()->point())) {
      // Removing the last vertex, i.e. a click on it. The vertex that the stroke ending now has added stays,
      // also if the stroke was too short to count as a drag.
      m_spline->lastVertex()->remove();
      if (!m_spline->firstVertex()) {
        // If it was the only vertex, cancelling spline creation.
        makePeerPreceeder(*m_context.createDefaultInteraction());
        m_context.imageView().update();
        delete this;
      }
    } else {
      // Adding a new vertex, provided we are not too close to the previous one.
      const Proximity prox(screenMousePos, toScreen.map(m_spline->lastVertex()->point()));
      if (prox > interaction.proximityThreshold()) {
        m_spline->appendVertex(imageMousePos);
        updateStatusTip();
      }
    }
  }

  event->accept();
}  // ZoneCreationInteraction::onMouseReleaseEvent

void ZoneCreationInteraction::onMouseMoveEvent(QMouseEvent* event, InteractionState& interaction) {
  const QPointF screenMousePos(event->pos() + QPointF(0.5, 0.5));
  const QTransform toScreen(m_context.imageView().imageToWidget());
  const QTransform fromScreen(m_context.imageView().widgetToImage());

  m_nextVertexImagePos = fromScreen.map(screenMousePos);
  const QPointF first(toScreen.map(m_spline->firstVertex()->point()));
  const QPointF last(toScreen.map(m_spline->lastVertex()->point()));

  ZoneCreationMode currentCreationMode = currentZoneCreationMode();

  if (Proximity(last, screenMousePos) <= interaction.proximityThreshold()) {
    m_nextVertexImagePos = m_spline->lastVertex()->point();
  } else if (m_spline->hasAtLeastSegments(2) || (currentCreationMode == ZoneCreationMode::RECTANGULAR)) {
    if (Proximity(first, screenMousePos) <= interaction.proximityThreshold()) {
      m_nextVertexImagePos = m_spline->firstVertex()->point();
    }
  }

  if ((currentCreationMode == ZoneCreationMode::RECTANGULAR)
      && (m_nextVertexImagePos != m_spline->firstVertex()->point())) {
    QPointF screenMousePosMid1;
    screenMousePosMid1.setX(first.x());
    screenMousePosMid1.setY(screenMousePos.y());

    QPointF screenMousePosMid2;
    screenMousePosMid2.setX(screenMousePos.x());
    screenMousePosMid2.setY(first.y());

    qreal dx = screenMousePos.x() - first.x();
    qreal dy = screenMousePos.y() - first.y();

    if (((dx > 0) && (dy > 0)) || ((dx < 0) && (dy < 0))) {
      m_nextVertexImagePos_mid1 = fromScreen.map(screenMousePosMid1);
      m_nextVertexImagePos_mid2 = fromScreen.map(screenMousePosMid2);
    } else {
      m_nextVertexImagePos_mid2 = fromScreen.map(screenMousePosMid1);
      m_nextVertexImagePos_mid1 = fromScreen.map(screenMousePosMid2);
    }
  }

  if ((currentCreationMode == ZoneCreationMode::LASSO) && m_leftMouseButtonPressed
      && (m_mouseButtonModifiers == Qt::NoModifier)) {
    Proximity minDistance = interaction.proximityThreshold();
    if ((Proximity(last, screenMousePos) > minDistance) && (Proximity(first, screenMousePos) > minDistance)) {
      m_spline->appendVertex(m_nextVertexImagePos);
      m_lassoVertexAddedSincePress = true;
    } else if (m_spline->hasAtLeastSegments(2) && (m_nextVertexImagePos == m_spline->firstVertex()->point())) {
      // Finishing the spline. Bridging the first and the last points
      // will create another segment.
      addFinishedZone();

      makePeerPreceeder(*m_context.createDefaultInteraction());
      m_context.imageView().update();
      delete this;
      // No member may be accessed from here on.
      return;
    }
  }

  // Also covers moving away from the first point again and mode switches while drawing.
  updateStatusTip();
  m_context.imageView().update();
}  // ZoneCreationInteraction::onMouseMoveEvent

void ZoneCreationInteraction::updateStatusTip() {
  QString tip;

  const bool atFirstVertex = (m_nextVertexImagePos == m_spline->firstVertex()->point());
  switch (currentZoneCreationMode()) {
    case ZoneCreationMode::RECTANGULAR:
      tip = tr("Rectangle selection: move to the opposite corner and click to finish. ESC to cancel.");
      break;
    case ZoneCreationMode::LASSO:
      if (m_spline->hasAtLeastSegments(2) && atFirstVertex) {
        tip = tr("Lasso selection: click here to close the zone. ESC to cancel.");
      } else {
        tip = tr(
            "Lasso selection: hold the left mouse button and draw around the area. "
            "The zone closes when you reach the starting point. ESC to cancel.");
      }
      break;
    case ZoneCreationMode::POLYGONAL:
      if (m_spline->hasAtLeastSegments(2) && atFirstVertex) {
        tip = tr("Polygon selection: click here to close the zone. ESC to cancel.");
      } else if (m_spline->hasAtLeastSegments(2)) {
        tip = tr(
            "Polygon selection: click to add a corner, click the first corner to close the zone. "
            "ESC to cancel.");
      } else {
        tip = tr(
            "Polygon selection: click to add corners. Clicking the last corner again removes it. "
            "X switches to lasso. ESC to cancel.");
      }
      break;
  }

  m_interaction.setInteractionStatusTip(tip);
}

bool ZoneCreationInteraction::isDragHandlerPermitted(const InteractionState& /*interaction*/) const {
  return !((currentZoneCreationMode() == ZoneCreationMode::LASSO) && m_leftMouseButtonPressed
           && (m_mouseButtonModifiers == Qt::NoModifier));
}

ZoneCreationMode ZoneCreationInteraction::currentZoneCreationMode() const {
  switch (m_initialZoneCreationMode) {
    case ZoneCreationMode::RECTANGULAR:
      return ZoneCreationMode::RECTANGULAR;
    case ZoneCreationMode::POLYGONAL:
    case ZoneCreationMode::LASSO:
      switch (m_context.getZoneCreationMode()) {
        case ZoneCreationMode::POLYGONAL:
          return ZoneCreationMode::POLYGONAL;
        case ZoneCreationMode::LASSO:
          return ZoneCreationMode::LASSO;
        default:
          break;
      }
  }
  return ZoneCreationMode::POLYGONAL;
}
