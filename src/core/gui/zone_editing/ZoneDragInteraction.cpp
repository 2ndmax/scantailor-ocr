// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoneDragInteraction.h"

#include <QApplication>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

#include "ImageViewBase.h"
#include "ZoneInteractionContext.h"

ZoneDragInteraction::ZoneDragInteraction(ZoneInteractionContext& context,
                                         InteractionState& interaction,
                                         const EditableSpline::Ptr& spline,
                                         const bool isNewCopy)
    : m_context(context), m_spline(spline), m_isNewCopy(isNewCopy) {
  m_initialMousePos = m_context.imageView().mapFromGlobal(QCursor::pos()) + QPointF(0.5, 0.5);

  if (m_spline) {
    if (SplineVertex::Ptr vertex = m_spline->firstVertex()) {
      do {
        m_initialVertexPositions.push_back(vertex->point());
        vertex = vertex->next(SplineVertex::LOOP);
      } while (vertex && (vertex != m_spline->firstVertex()));
    }
  }

  m_interaction.setInteractionStatusTip(tr("Release the left mouse button to finish dragging."));
  m_interaction.setInteractionCursor(Qt::DragMoveCursor);
  interaction.capture(m_interaction);
}

void ZoneDragInteraction::onPaint(QPainter& painter, const InteractionState& /*interaction*/) {
  painter.setWorldMatrixEnabled(false);
  painter.setRenderHint(QPainter::Antialiasing);

  const QTransform toScreen(m_context.imageView().imageToWidget());

  for (const EditableZoneSet::Zone& zone : m_context.zones()) {
    const EditableSpline::Ptr& spline = zone.spline();
    if (spline != m_spline) {
      m_visualizer.drawSpline(painter, toScreen, spline);
      continue;
    }
    // The zone being dragged stays as strong as it was under the mouse.
    m_visualizer.prepareForActiveSpline(painter, spline);
    painter.drawPolygon(toScreen.map(spline->toPolygon()), Qt::WindingFill);
  }
}

void ZoneDragInteraction::onMouseReleaseEvent(QMouseEvent* event, InteractionState& /*interaction*/) {
  if (event->button() == Qt::LeftButton) {
    if (m_isNewCopy && (m_largestShift < QApplication::startDragDistance())) {
      // A click on the zone with the copy keys only: the copy would lie exactly on the original, unseen.
      m_context.zones().removeZone(m_spline);
    } else {
      m_context.zones().commit();
    }
    makePeerPreceeder(*m_context.createDefaultInteraction());
    m_context.imageView().updateViewport();
    event->accept();
    delete this;
  }
}

void ZoneDragInteraction::onMouseMoveEvent(QMouseEvent* event, InteractionState& /*interaction*/) {
  if (!m_spline || !m_spline->firstVertex() || m_initialVertexPositions.empty()) {
    return;
  }

  const QTransform fromScreen(m_context.imageView().widgetToImage());
  const QPointF screenPos(event->pos() + QPointF(0.5, 0.5));
  const QPointF shift = screenPos - m_initialMousePos;
  m_largestShift = std::max(m_largestShift, std::hypot(shift.x(), shift.y()));
  const QPointF imageShift = fromScreen.map(screenPos) - fromScreen.map(m_initialMousePos);

  SplineVertex::Ptr vertex(m_spline->firstVertex());
  size_t idx = 0;
  do {
    vertex->setPoint(m_initialVertexPositions[idx++] + imageShift);
    vertex = vertex->next(SplineVertex::LOOP);
  } while (vertex && (vertex != m_spline->firstVertex()) && (idx < m_initialVertexPositions.size()));

  m_context.imageView().updateViewport();
}  // ZoneDragInteraction::onMouseMoveEvent
