// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ThumbnailBase.h"

#include <PolygonUtils.h>

#include <QApplication>
#include <QCoreApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPixmapCache>
#include <QStyleOptionGraphicsItem>
#include <algorithm>
#include <cmath>
#include <utility>

#include "ApplicationSettings.h"
#include "ColorSchemeManager.h"
#include "PixmapRenderer.h"

using namespace imageproc;

class ThumbnailBase::LoadCompletionHandler : public AbstractCommand<void, const ThumbnailLoadResult&> {
  DECLARE_NON_COPYABLE(LoadCompletionHandler)

 public:
  explicit LoadCompletionHandler(ThumbnailBase* thumb) : m_thumb(thumb) {}

  void operator()(const ThumbnailLoadResult& result) override { m_thumb->handleLoadResult(result); }

 private:
  ThumbnailBase* m_thumb;
};


ThumbnailBase::ThumbnailBase(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                             const QSizeF& maxSize,
                             const ImageId& imageId,
                             const ImageTransformation& imageXform)
    : ThumbnailBase(std::move(thumbnailCache), maxSize, imageId, imageXform, imageXform.resultingRect()) {}

ThumbnailBase::ThumbnailBase(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                             const QSizeF& maxSize,
                             const ImageId& imageId,
                             const ImageTransformation& imageXform,
                             QRectF displayArea)
    : m_thumbnailCache(std::move(thumbnailCache)),
      m_maxSize(maxSize),
      m_imageId(imageId),
      m_imageXform(imageXform),
      m_displayArea(displayArea) {
  setImageXform(m_imageXform);
}

ThumbnailBase::~ThumbnailBase() = default;

QRectF ThumbnailBase::boundingRect() const {
  return m_boundingRect;
}

void ThumbnailBase::paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) {
  QPixmap pixmap;

  if (!m_completionHandler) {
    auto handler = std::make_shared<LoadCompletionHandler>(this);
    const ThumbnailPixmapCache::Status status = m_thumbnailCache->loadRequest(m_imageId, pixmap, handler);
    if (status == ThumbnailPixmapCache::QUEUED) {
      m_completionHandler.swap(handler);
    }
  }

  const QTransform thumbToDisplay = painter->worldTransform();
  const QTransform imageToDisplay = m_postScaleXform * thumbToDisplay;

  if (pixmap.isNull()) {
    const double border = 1.0;
    const double shadow = 2.0;
    QRectF rect(m_boundingRect);
    rect.adjust(border, border, -(border + shadow), -(border + shadow));

    painter->fillRect(m_boundingRect, QColor(0x00, 0x00, 0x00));
    painter->fillRect(rect, QColor(0xff, 0xff, 0xff));

    paintOverImage(*painter, imageToDisplay, thumbToDisplay);
    return;
  }

  const QSizeF origImageSize(m_imageXform.origRect().size());
  const double xPreScale = origImageSize.width() / pixmap.width();
  const double yPreScale = origImageSize.height() / pixmap.height();
  QTransform preScaleXform;
  preScaleXform.scale(xPreScale, yPreScale);

  const QTransform pixmapToThumb(preScaleXform * m_imageXform.transform() * m_postScaleXform);

  // The polygon to draw into in original image coordinates.
  QPolygonF imagePoly
      = PolygonUtils::round(m_imageXform.resultingPreCropArea().intersected(m_imageXform.resultingRect()));
  // The polygon to draw into in display coordinates.
  QPolygonF displayPoly(imageToDisplay.map(imagePoly));

  QRectF displayRect(imageToDisplay.map(PolygonUtils::round(m_displayArea)).boundingRect().toAlignedRect());

  // The temporary pixmap has screen pixels, so that thumbnails stay sharp on scaled screens.
  const qreal dpr = painter->device() ? painter->device()->devicePixelRatioF() : 1.0;
  QPixmap tempPixmap;
  const QString cacheKey(QString::fromLatin1("ThumbnailBase::tempPixmap"));
  if (!QPixmapCache::find(cacheKey, &tempPixmap) || (tempPixmap.devicePixelRatio() != dpr)
      || (tempPixmap.deviceIndependentSize().width() < displayRect.width())
      || (tempPixmap.deviceIndependentSize().height() < displayRect.height())) {
    auto w = (int) displayRect.width();
    auto h = (int) displayRect.height();
    // Add some extra, to avoid rectreating the pixmap too often.
    w += w / 10;
    h += h / 10;

    tempPixmap = QPixmap(static_cast<int>(std::ceil(w * dpr)), static_cast<int>(std::ceil(h * dpr)));
    tempPixmap.setDevicePixelRatio(dpr);
  }
  // While the cache shares the pixmap, painting on it would copy it first. So it is taken out of the
  // cache for painting and put back afterwards.
  QPixmapCache::remove(cacheKey);
  // Each thumbnail starts from a transparent pixmap: the image is drawn in Source mode, and its
  // antialiased edges would keep what the previous thumbnail left. This also creates the alpha channel.
  tempPixmap.fill(Qt::transparent);

  QPainter tempPainter;
  tempPainter.begin(&tempPixmap);

  QTransform tempAdjustment;
  tempAdjustment.translate(-displayRect.left(), -displayRect.top());

  tempPainter.setWorldTransform(pixmapToThumb * thumbToDisplay * tempAdjustment);
  // Turn off alpha compositing.
  tempPainter.setCompositionMode(QPainter::CompositionMode_Source);
  tempPainter.setRenderHint(QPainter::SmoothPixmapTransform);
  tempPainter.setRenderHint(QPainter::Antialiasing);
  PixmapRenderer::drawPixmap(tempPainter, pixmap);

  // Turn alpha compositing on again.
  tempPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  // Setup the painter for drawing in thumbnail coordinates,
  // as required for paintOverImage().
  tempPainter.setWorldTransform(thumbToDisplay * tempAdjustment);
  tempPainter.save();
  prePaintOverImage(tempPainter, imageToDisplay * tempAdjustment, thumbToDisplay * tempAdjustment);
  tempPainter.restore();

  tempPainter.setPen(Qt::NoPen);
  tempPainter.setBrush(Qt::transparent);
  tempPainter.setWorldTransform(tempAdjustment);
  tempPainter.setCompositionMode(QPainter::CompositionMode_Clear);
  {
    QPainterPath outerPath;
    outerPath.addRect(displayRect);
    QPainterPath innerPath;
    innerPath.addPolygon(displayPoly);
    tempPainter.drawPath(outerPath.subtracted(innerPath));
  }

  // Turn alpha compositing on again.
  tempPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
  // Setup the painter for drawing in thumbnail coordinates,
  // as required for paintOverImage().
  tempPainter.setWorldTransform(thumbToDisplay * tempAdjustment);
  tempPainter.save();
  paintOverImage(tempPainter, imageToDisplay * tempAdjustment, thumbToDisplay * tempAdjustment);
  tempPainter.restore();

  tempPainter.end();
  QPixmapCache::insert(cacheKey, tempPixmap);

  painter->setWorldTransform(QTransform());
  painter->setClipRect(displayRect);
  painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
  painter->setCompositionMode(QPainter::CompositionMode_SourceOver);
  painter->drawPixmap(displayRect.topLeft(), tempPixmap);
}  // ThumbnailBase::paint

namespace {
/**
 * A mark at the top right corner of a thumbnail. It ignores the scale of the thumbnail, so that
 * all marks are equally large on the screen, and has its own tooltip.
 */
class ThumbnailMark : public QGraphicsItem {
 public:
  enum { Type = UserType + 1 };

  ThumbnailMark(QGraphicsItem* parent, const ThumbnailBase::Mark mark) : QGraphicsItem(parent), m_mark(mark) {
    setFlag(ItemIgnoresTransformations);
    // At the right edge of the parent; ThumbnailSequence lines the marks of all thumbnails up.
    setPos(parent->boundingRect().topRight());
    setToolTip(
        (mark == ThumbnailBase::INCOMPLETE_MARK)
            ? QCoreApplication::translate("ThumbnailBase", "This page is not processed in this step yet.")
            : QCoreApplication::translate("ThumbnailBase", "This page deviates strongly from the others. Check it."));
  }

  int type() const override { return Type; }

  QRectF boundingRect() const override { return ThumbnailBase::markRect(QRectF()); }

  // Hidden marks of deviation don't show their tooltip either.
  QPainterPath shape() const override {
    QPainterPath path;
    if (isShown()) {
      path.addRect(boundingRect());
    }
    return path;
  }

  void paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*) override {
    if (isShown()) {
      ThumbnailBase::drawMark(*painter, boundingRect(), m_mark);
    }
  }

 private:
  bool isShown() const {
    return (m_mark != ThumbnailBase::DEVIANT_MARK) || ApplicationSettings::getInstance().isHighlightDeviationEnabled();
  }

  ThumbnailBase::Mark m_mark;
};
}  // namespace

void ThumbnailBase::addMark(QGraphicsItem* item, const Mark mark) {
  new ThumbnailMark(item, mark);
}

void ThumbnailBase::alignMarks(QGraphicsItem* item, const double sceneRight) {
  // Never left of the item's own right edge.
  const double right = std::max(item->boundingRect().right(), item->mapFromScene(QPointF(sceneRight, 0)).x());
  for (QGraphicsItem* child : item->childItems()) {
    if (child->type() == ThumbnailMark::Type) {
      child->setX(right);
    }
  }
}

QRectF ThumbnailBase::markRect(const QRectF& rect) {
  const double margin = 3;
  return QRectF(rect.right() - margin - MARK_SIZE, rect.top() + margin, MARK_SIZE, MARK_SIZE);
}

void ThumbnailBase::drawMark(QPainter& painter, const QRectF& square, const Mark mark) {
  painter.save();
  painter.setRenderHint(QPainter::Antialiasing);
  const double s = square.width();
  // A light square keeps the mark readable on any image.
  QPen framePen(QColor(0, 0, 0, 70));
  framePen.setCosmetic(true);
  painter.setPen(framePen);
  painter.setBrush(QColor(255, 255, 255, 230));
  // Square or rounded corners, as the buttons of the color scheme; a slight rounding keeps
  // a small square from looking frayed.
  const double radius = ColorSchemeManager::instance().hasRoundedCorners() ? s * 0.2 : 1.0;
  painter.drawRoundedRect(square, radius, radius);

  // Both marks are in the accent color, as the frame of the selected thumbnail; their shapes tell them apart.
  const QColor accent = ColorSchemeManager::instance().getColorParam("ThumbnailSequenceSelectedItemBorder");
  if (mark == INCOMPLETE_MARK) {
    // An hourglass: two triangles meeting in the middle, closed by a bar at the top and at the bottom.
    const double x0 = square.left() + s * 0.28;
    const double x1 = square.right() - s * 0.28;
    const double y0 = square.top() + s * 0.2;
    const double y1 = square.bottom() - s * 0.2;
    const QPointF middle = square.center();
    QPainterPath glass;
    glass.moveTo(x0, y0);
    glass.lineTo(x1, y0);
    glass.lineTo(middle);
    glass.lineTo(x1, y1);
    glass.lineTo(x0, y1);
    glass.lineTo(middle);
    glass.closeSubpath();
    QPen pen(accent);
    pen.setWidthF(std::max(1.0, s * 0.08));
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(accent);
    painter.drawPath(glass);
  } else {
    // An exclamation mark: a bar and a dot.
    const double barWidth = s * 0.16;
    painter.setPen(Qt::NoPen);
    painter.setBrush(accent);
    painter.drawRoundedRect(QRectF(square.center().x() - barWidth / 2, square.top() + s * 0.16, barWidth, s * 0.46),
                            barWidth / 2, barWidth / 2);
    painter.drawEllipse(QPointF(square.center().x(), square.bottom() - s * 0.2), barWidth * 0.62, barWidth * 0.62);
  }
  painter.restore();
}

void ThumbnailBase::setImageXform(const ImageTransformation& imageXform) {
  // REVIEW-NOTE (2026-10): m_displayArea is not updated here -> a changed transform would use the old display
  // area. Not fixed because only the constructor calls it.
  m_imageXform = imageXform;
  const QSizeF unscaledSize(m_displayArea.size().expandedTo(QSizeF(1, 1)));
  QSizeF scaledSize(unscaledSize);
  scaledSize.scale(m_maxSize, Qt::KeepAspectRatio);

  m_boundingRect = QRectF(QPointF(0.0, 0.0), scaledSize);

  const double xPostScale = scaledSize.width() / unscaledSize.width();
  const double yPostScale = scaledSize.height() / unscaledSize.height();
  m_postScaleXform.reset();
  m_postScaleXform.scale(xPostScale, yPostScale);
}

void ThumbnailBase::handleLoadResult(const ThumbnailLoadResult& result) {
  m_completionHandler.reset();

  if (result.status() != ThumbnailLoadResult::LOAD_FAILED) {
    // Note that we don't store result.pixmap() in
    // this object, because we may have already went
    // out of view, so we may never receive a paint event.
    update();
  }
}
