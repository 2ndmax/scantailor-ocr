// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "BasicSplineVisualizer.h"

#include <QLinearGradient>
#include <QPainter>
#include <QTransform>

#include "EditableZoneSet.h"
#include "OverlayStyle.h"

BasicSplineVisualizer::BasicSplineVisualizer()
    : m_solidColor(OverlayStyle::secondary().rgb()),
      m_highlightBrightColor(OverlayStyle::primary().rgb()),
      m_highlightDarkColor(OverlayStyle::primary().rgb()),
      m_pen(OverlayStyle::pen(m_solidColor, OverlayStyle::Line::Fine)) {}

void BasicSplineVisualizer::drawSplines(QPainter& painter, const QTransform& toScreen, const EditableZoneSet& zones) {
  for (const EditableZoneSet::Zone& zone : zones) {
    drawSpline(painter, toScreen, zone.spline());
  }
}

void BasicSplineVisualizer::drawSpline(QPainter& painter,
                                       const QTransform& toScreen,
                                       const EditableSpline::Ptr& spline) {
  prepareForSpline(painter, spline);
  painter.drawPolygon(toScreen.map(spline->toPolygon()), Qt::WindingFill);
}

void BasicSplineVisualizer::drawVertex(QPainter& painter, const QPointF& pt, const QColor& color) {
  painter.setPen(Qt::NoPen);
  painter.setBrush(color);

  QRectF rect(0, 0, 4, 4);
  rect.moveCenter(pt);
  painter.drawEllipse(rect);
}

QPolygonF BasicSplineVisualizer::screenPolylineSkipping(const SplineVertex::Ptr& vertex, const QTransform& toScreen) {
  QPolygonF points;
  SplineVertex::Ptr other(vertex->next(SplineVertex::LOOP));
  for (; other != vertex; other = other->next(SplineVertex::LOOP)) {
    points.push_back(toScreen.map(other->point()));
  }
  return points;
}

void BasicSplineVisualizer::drawHighlightedVertex(QPainter& painter,
                                                  const QTransform& toScreen,
                                                  const SplineVertex::Ptr& vertex,
                                                  QPen pen) {
  QLinearGradient gradient;  // From the neighbor to the highlighted vertex.
  gradient.setColorAt(0.0, m_solidColor);
  gradient.setColorAt(1.0, m_highlightDarkColor);

  const QPointF prev(toScreen.map(vertex->prev(SplineVertex::LOOP)->point()));
  const QPointF pt(toScreen.map(vertex->point()));
  const QPointF next(toScreen.map(vertex->next(SplineVertex::LOOP)->point()));

  gradient.setStart(prev);
  gradient.setFinalStop(pt);
  pen.setBrush(gradient);
  painter.setPen(pen);
  painter.drawLine(prev, pt);

  gradient.setStart(next);
  pen.setBrush(gradient);
  painter.setPen(pen);
  painter.drawLine(next, pt);

  drawVertex(painter, pt, m_highlightBrightColor);
}

void BasicSplineVisualizer::prepareForSpline(QPainter& painter, const EditableSpline::Ptr&) {
  painter.setPen(m_pen);
  painter.setBrush(Qt::NoBrush);
}

void BasicSplineVisualizer::prepareForActiveSpline(QPainter& painter, const EditableSpline::Ptr& spline) {
  prepareForSpline(painter, spline);
  QPen pen(painter.pen());
  pen.setWidthF(OverlayStyle::lineWidth(OverlayStyle::Line::Normal));
  painter.setPen(pen);
}

QPen BasicSplineVisualizer::activePen() const {
  return OverlayStyle::pen(m_solidColor, OverlayStyle::Line::Normal);
}
