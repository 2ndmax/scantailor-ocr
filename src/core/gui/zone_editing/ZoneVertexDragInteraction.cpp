// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoneVertexDragInteraction.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <cmath>

#include "ImageViewBase.h"
#include "SerializableSpline.h"
#include "Zone.h"
#include "ZoneInteractionContext.h"

ZoneVertexDragInteraction::ZoneVertexDragInteraction(ZoneInteractionContext& context,
                                                     InteractionState& interaction,
                                                     const EditableSpline::Ptr& spline,
                                                     const SplineVertex::Ptr& vertex)
    : m_context(context), m_spline(spline), m_vertex(vertex) {
  m_origPrev = m_vertex->prev(SplineVertex::LOOP);
  m_origNext = m_vertex->next(SplineVertex::LOOP);
  m_origVertexPos = m_vertex->point();
  m_origPrevPos = m_origPrev ? m_origPrev->point() : QPointF();
  m_origNextPos = m_origNext ? m_origNext->point() : QPointF();

  const QPointF screenMousePos(m_context.imageView().mapFromGlobal(QCursor::pos()) + QPointF(0.5, 0.5));
  const QTransform toScreen(m_context.imageView().imageToWidget());
  m_dragOffset = toScreen.map(vertex->point()) - screenMousePos;

  interaction.capture(m_interaction);
  checkProximity(interaction);
}

void ZoneVertexDragInteraction::onPaint(QPainter& painter, const InteractionState& /*interaction*/) {
  painter.setWorldMatrixEnabled(false);
  painter.setRenderHint(QPainter::Antialiasing);

  const QTransform toScreen(m_context.imageView().imageToWidget());

  for (const EditableZoneSet::Zone& zone : m_context.zones()) {
    const EditableSpline::Ptr& spline = zone.spline();

    if (spline != m_spline) {
      // Draw the whole spline in solid color.
      m_visualizer.drawSpline(painter, toScreen, spline);
      continue;
    }
    // Draw the solid part of the spline.
    const QPolygonF points(BasicSplineVisualizer::screenPolylineSkipping(m_vertex, toScreen));

    m_visualizer.prepareForActiveSpline(painter, spline);
    painter.drawPolyline(points);
  }

  painter.setBrush(Qt::NoBrush);
  m_visualizer.drawHighlightedVertex(painter, toScreen, m_vertex, m_visualizer.activePen());
}  // ZoneVertexDragInteraction::onPaint

void ZoneVertexDragInteraction::onMouseReleaseEvent(QMouseEvent* event, InteractionState& /*interaction*/) {
  if (event->button() == Qt::LeftButton) {
    const auto restoreSaved = [this]() {
      m_vertex->setPoint(m_origVertexPos);
      if (m_origPrev) {
        m_origPrev->setPoint(m_origPrevPos);
      }
      if (m_origNext) {
        m_origNext->setPoint(m_origNextPos);
      }
    };

    const bool mergeRequested = ((m_vertex->point() == m_vertex->next(SplineVertex::LOOP)->point())
                                 || (m_vertex->point() == m_vertex->prev(SplineVertex::LOOP)->point()))
                                && m_vertex->hasAtLeastSiblings(3);
    if (mergeRequested) {
      QPolygonF polyWithoutVertex;
      if (SplineVertex::Ptr v = m_spline->firstVertex()) {
        do {
          if (v != m_vertex) {
            polyWithoutVertex.push_back(v->point());
          }
          v = v->next(SplineVertex::LOOP);
        } while (v && (v != m_spline->firstVertex()));
      }
      if (Zone(SerializableSpline(polyWithoutVertex)).isValid()) {
        m_vertex->remove();
        m_context.zones().commit();
      } else {
        restoreSaved();
      }
    } else if (Zone(SerializableSpline(*m_spline)).isValid()) {
      m_context.zones().commit();
    } else {
      restoreSaved();
    }

    makePeerPreceeder(*m_context.createDefaultInteraction());
    m_context.imageView().updateViewport();
    event->accept();
    delete this;
  }
}

void ZoneVertexDragInteraction::onMouseMoveEvent(QMouseEvent* event, InteractionState& interaction) {
  const QTransform fromScreen(m_context.imageView().widgetToImage());
  m_vertex->setPoint(fromScreen.map(event->pos() + QPointF(0.5, 0.5) + m_dragOffset));

  const bool canMerge = checkProximity(interaction);

  const Qt::KeyboardModifiers mask = event->modifiers();
  if ((mask == Qt::ControlModifier) && !canMerge) {
    const QTransform toScreen(m_context.imageView().imageToWidget());

    const QPointF current = toScreen.map(m_vertex->point());
    QPointF prev = toScreen.map(m_vertex->prev(SplineVertex::LOOP)->point());
    QPointF next = toScreen.map(m_vertex->next(SplineVertex::LOOP)->point());

    const QPointF prevDelta = prev - current;
    const QPointF nextDelta = next - current;
    const double prevLenSq = prevDelta.x() * prevDelta.x() + prevDelta.y() * prevDelta.y();
    const double nextLenSq = nextDelta.x() * nextDelta.x() + nextDelta.y() * nextDelta.y();

    if ((prevLenSq > 0.0) || (nextLenSq > 0.0)) {
      constexpr double kInvSqrt2 = 0.70710678118654752440;
      bool verticalPrev = false;
      if ((prevLenSq > 0.0) && (nextLenSq > 0.0)) {
        const double prevAngleCos = std::abs(prevDelta.x()) / std::sqrt(prevLenSq);
        const double nextAngleCos = std::abs(nextDelta.x()) / std::sqrt(nextLenSq);
        verticalPrev = (prevAngleCos < nextAngleCos);
      } else if (prevLenSq == 0.0) {
        const double nextAngleCos = std::abs(nextDelta.x()) / std::sqrt(nextLenSq);
        verticalPrev = (nextAngleCos > kInvSqrt2);
      } else {
        const double prevAngleCos = std::abs(prevDelta.x()) / std::sqrt(prevLenSq);
        verticalPrev = (prevAngleCos < kInvSqrt2);
      }

      if (verticalPrev) {
        prev.setX(current.x());
        next.setY(current.y());
      } else {
        next.setX(current.x());
        prev.setY(current.y());
      }

      m_vertex->prev(SplineVertex::LOOP)->setPoint(fromScreen.map(prev));
      m_vertex->next(SplineVertex::LOOP)->setPoint(fromScreen.map(next));
    }
  }

  m_context.imageView().updateViewport();
}  // ZoneVertexDragInteraction::onMouseMoveEvent

bool ZoneVertexDragInteraction::checkProximity(const InteractionState& interaction) {
  bool canMerge = false;

  if (m_vertex->hasAtLeastSiblings(3)) {
    const QTransform toScreen(m_context.imageView().imageToWidget());
    const QPointF origin(toScreen.map(m_vertex->point()));

    const QPointF prev(m_vertex->prev(SplineVertex::LOOP)->point());
    const Proximity proxPrev(origin, toScreen.map(prev));

    const QPointF next(m_vertex->next(SplineVertex::LOOP)->point());
    const Proximity proxNext(origin, toScreen.map(next));

    if ((proxPrev <= interaction.proximityThreshold()) && (proxPrev < proxNext)) {
      m_vertex->setPoint(prev);
      canMerge = true;
    } else if (proxNext <= interaction.proximityThreshold()) {
      m_vertex->setPoint(next);
      canMerge = true;
    }
  }

  if (canMerge) {
    m_interaction.setInteractionStatusTip(tr("Release to merge the two corners."));
  } else {
    m_interaction.setInteractionStatusTip(tr("Move the corner to one of its neighbors to merge them."));
  }
  return canMerge;
}
