// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PageLayoutAdapter.h"

#include <cmath>

namespace page_split {

QLineF PageLayoutAdapter::adaptCutter(const QLineF& cutterLine, const QRectF& newRect) {
  if (!newRect.isValid() || cutterLine.isNull()) {
    return cutterLine;
  }

  QLineF upperBorder(newRect.topLeft(), newRect.topRight());
  QPointF upperIntersection;
  {
    auto is = upperBorder.intersects(cutterLine, &upperIntersection);
    if (is == QLineF::NoIntersection) {
      return cutterLine;
    }
  }
  // if intersection is outside the rect
  if (upperIntersection.x() < newRect.topLeft().x()) {
    upperIntersection.setX(newRect.topLeft().x());
  } else if (upperIntersection.x() > newRect.topRight().x()) {
    upperIntersection.setX(newRect.topRight().x());
  }

  QLineF lowerBorder(newRect.bottomLeft(), newRect.bottomRight());
  QPointF lowerIntersection;
  {
    auto is = lowerBorder.intersects(cutterLine, &lowerIntersection);
    if (is == QLineF::NoIntersection) {
      return cutterLine;
    }
  }
  // if intersection is outside the rect
  if (lowerIntersection.x() < newRect.bottomLeft().x()) {
    lowerIntersection.setX(newRect.bottomLeft().x());
  } else if (lowerIntersection.x() > newRect.bottomRight().x()) {
    lowerIntersection.setX(newRect.bottomRight().x());
  }
  return QLineF(upperIntersection, lowerIntersection);
}

QVector<QLineF> PageLayoutAdapter::adaptCutters(const QVector<QLineF>& cuttersList, const QRectF& newRect) {
  QVector<QLineF> adaptedCutters;

  for (QLineF cutter : cuttersList) {
    QLineF adaptedCutter = adaptCutter(cutter, newRect);
    // A line that could not be adapted comes back as it was, with its points in any order.
    // Sorting by x1 and the correction below need p1 to be the upper point.
    if (adaptedCutter.y1() > adaptedCutter.y2()) {
      adaptedCutter = QLineF(adaptedCutter.p2(), adaptedCutter.p1());
    }
    adaptedCutters.append(adaptedCutter);
  }

  std::sort(adaptedCutters.begin(), adaptedCutters.end(),
            [](const QLineF& line1, const QLineF& line2) -> bool { return line1.x1() < line2.x1(); });

  // checking whether the cutters intersect each other inside the new rect, and if so fixing that
  const qreal upperBound = newRect.top();
  const qreal lowerBound = newRect.bottom();
  for (int i = 1; i < adaptedCutters.size(); i++) {
    QPointF intersection;
    QLineF cutterLeft = adaptedCutters.at(i - 1);
    QLineF cutterRight = adaptedCutters.at(i);
    auto is = cutterLeft.intersects(cutterRight, &intersection);
    if (is == QLineF::NoIntersection) {
      continue;
    }

    if ((intersection.y() < lowerBound) && (intersection.y() > upperBound)) {
      if ((lowerBound - intersection.y()) <= (lowerBound - upperBound) / 2) {
        qreal newY = lowerBound;
        cutterLeft.setP2(QPointF(intersection.x(), newY));
        cutterRight.setP2(QPointF(intersection.x(), newY));
      } else {
        qreal newY = upperBound;
        cutterLeft.setP1(QPointF(intersection.x(), newY));
        cutterRight.setP1(QPointF(intersection.x(), newY));
      }
      adaptedCutters.replace(i - 1, cutterLeft);
      adaptedCutters.replace(i, cutterRight);
    }
  }
  return adaptedCutters;
}

void PageLayoutAdapter::correctPageLayoutType(PageLayout* layout, const bool allowFullPageCut) {
  constexpr double kEps = 1e-4;
  auto isDegenerate = [](const QLineF& line) { return line.isNull() || line.length() <= 1e-9; };

  const QRectF outline = layout->uncutOutline().boundingRect();
  if (outline.isEmpty()) {
    return;
  }
  auto onVerticalEdge = [&](const QLineF& line) {
    return (std::abs(line.x1() - outline.left()) <= kEps && std::abs(line.x2() - outline.left()) <= kEps)
           || (std::abs(line.x1() - outline.right()) <= kEps && std::abs(line.x2() - outline.right()) <= kEps);
  };

  if (layout->type() == PageLayout::SINGLE_PAGE_CUT) {
    if (isDegenerate(layout->cutterLine(0)) || isDegenerate(layout->cutterLine(1))) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
      return;
    }

    const QLineF cutterLine1 = layout->inscribedCutterLine(0);
    const QLineF cutterLine2 = layout->inscribedCutterLine(1);
    if (isDegenerate(cutterLine1) || isDegenerate(cutterLine2)) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
      return;
    }

    // if both cutter lines match left or right bound
    if (!allowFullPageCut && onVerticalEdge(cutterLine1) && onVerticalEdge(cutterLine2)) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
      return;
    }

    // if cutter lines match/overlap (collinear) or intersect inside outline (not valid)
    QPointF intersection;
    const QLineF::IntersectType intersectType = cutterLine1.intersects(cutterLine2, &intersection);
    bool invalid = false;
    if (intersectType != QLineF::NoIntersection) {
      invalid = (intersection.y() > outline.top() + kEps) && (intersection.y() < outline.bottom() - kEps);
    } else {
      const QPointF d = cutterLine1.p2() - cutterLine1.p1();
      const QPointF v = cutterLine2.p1() - cutterLine1.p1();
      const double cross = std::abs(d.x() * v.y() - d.y() * v.x());
      invalid = (cross / cutterLine1.length() <= kEps);
    }
    if (invalid) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
    }
    return;
  }

  if (layout->type() == PageLayout::TWO_PAGES) {
    if (isDegenerate(layout->cutterLine(0))) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
      return;
    }

    const QLineF cutterLine1 = layout->inscribedCutterLine(0);
    // if the cutter line is degenerate or matches left or right bound
    if (isDegenerate(cutterLine1) || onVerticalEdge(cutterLine1)) {
      layout->setType(PageLayout::SINGLE_PAGE_UNCUT);
    }
  }
}

PageLayout PageLayoutAdapter::adaptPageLayout(const PageLayout& pageLayout,
                                              const QRectF& outline,
                                              const bool allowFullPageCut) {
  if (pageLayout.uncutOutline().boundingRect() == outline) {
    return pageLayout;
  }

  PageLayout newPageLayout;

  if (pageLayout.type() == PageLayout::SINGLE_PAGE_CUT) {
    const QVector<QLineF> adaptedCutters
        = PageLayoutAdapter::adaptCutters({pageLayout.cutterLine(0), pageLayout.cutterLine(1)}, outline);
    newPageLayout = PageLayout(outline, adaptedCutters.at(0), adaptedCutters.at(1));
    correctPageLayoutType(&newPageLayout, allowFullPageCut);
  } else if (pageLayout.type() == PageLayout::TWO_PAGES) {
    QLineF adaptedCutter = PageLayoutAdapter::adaptCutter(pageLayout.cutterLine(0), outline);
    newPageLayout = PageLayout(outline, adaptedCutter);
    correctPageLayoutType(&newPageLayout, allowFullPageCut);
  } else {
    newPageLayout = PageLayout(outline);
  }
  return newPageLayout;
}
}  // namespace page_split
