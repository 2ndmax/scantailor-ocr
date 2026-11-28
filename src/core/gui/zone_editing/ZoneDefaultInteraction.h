// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_ZONES_ZONEDEFAULTINTERACTION_H_
#define SCANTAILOR_ZONES_ZONEDEFAULTINTERACTION_H_

#include <QCoreApplication>
#include <QPointF>
#include <memory>

#include "BasicSplineVisualizer.h"
#include "DragHandler.h"
#include "DragWatcher.h"
#include "EditableSpline.h"
#include "InteractionHandler.h"
#include "InteractionState.h"
#include "SplineSegment.h"
#include "SplineVertex.h"

class QShortcut;
class ZoneInteractionContext;

class ZoneDefaultInteraction : public InteractionHandler {
  Q_DECLARE_TR_FUNCTIONS(ZoneDefaultInteraction)
 public:
  explicit ZoneDefaultInteraction(ZoneInteractionContext& context);

 protected:
  ZoneInteractionContext& context() { return m_context; }

  void onPaint(QPainter& painter, const InteractionState& interaction) override;

  void onProximityUpdate(const QPointF& mousePos, InteractionState& interaction) override;

  void onMousePressEvent(QMouseEvent* event, InteractionState& interaction) override;

  void onMouseReleaseEvent(QMouseEvent* event, InteractionState& interaction) override;

  void onMouseMoveEvent(QMouseEvent* event, InteractionState& interaction) override;

  void onContextMenuEvent(QContextMenuEvent* event, InteractionState& interaction) override;

 private:
  void switchToInteraction(InteractionHandler* next, QEvent* event = nullptr);

  void clearHoverCache();

  void removeZoneUnderMouse();

  void removeVertexUnderMouse();

  ZoneInteractionContext& m_context;
  BasicSplineVisualizer m_visualizer;
  InteractionState::Captor m_vertexProximity;
  InteractionState::Captor m_segmentProximity;
  InteractionState::Captor m_zoneAreaProximity;
  InteractionState::Captor m_zoneAreaDragProximity;
  InteractionState::Captor m_zoneAreaDragCopyProximity;
  QPointF m_screenMousePos;

  /**
   * We want our own drag handler, to be able to monitor it
   * and decide if we should go into zone creation state
   * after the left mouse button is released.
   */
  DragHandler m_dragHandler;

  /**
   * Watches m_dragHandler so onMouseReleaseEvent() can distinguish a click from a drag.
   */
  DragWatcher m_dragWatcher;

  // These are valid if m_vertexProximity is the proximity leader.
  SplineVertex::Ptr m_nearestVertex;
  EditableSpline::Ptr m_nearestVertexSpline;

  // These are valid if m_segmentProximity is the proximity leader.
  SplineSegment m_nearestSegment;
  EditableSpline::Ptr m_nearestSegmentSpline;
  QPointF m_screenPointOnSegment;
  EditableSpline::Ptr m_splineUnderMouse;

  std::unique_ptr<QShortcut> m_removeZoneShortcut;
  std::unique_ptr<QShortcut> m_removeVertexShortcut;
};


#endif  // ifndef SCANTAILOR_ZONES_ZONEDEFAULTINTERACTION_H_
