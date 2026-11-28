// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "HqImageCache.h"

#include <Transform.h>

#include <QAtomicInt>
#include <QPoint>
#include <QPointer>
#include <QRectF>
#include <Qt>
#include <utility>

#include "AbstractCommand.h"
#include "BackgroundExecutor.h"
#include "NonCopyable.h"

using namespace imageproc;

class HqImageCache::HqTransformTask : public AbstractCommand<std::shared_ptr<AbstractCommand<void>>> {
  DECLARE_NON_COPYABLE(HqTransformTask)

 public:
  HqTransformTask(HqImageCache* cache, const QImage& image, const QTransform& xform, const QSize& targetSize);

  void cancel() { m_result->cancel(); }

  bool isCancelled() const { return m_result->isCancelled(); }

  std::shared_ptr<AbstractCommand<void>> operator()() override;

 private:
  class Result : public AbstractCommand<void> {
   public:
    explicit Result(HqImageCache* cache);

    void setData(const QPoint& origin, const QImage& hqImage);

    void cancel() { m_cancelFlag.fetchAndStoreRelaxed(1); }

    bool isCancelled() const { return m_cancelFlag.fetchAndAddRelaxed(0) != 0; }

    void operator()() override;

   private:
    QPointer<HqImageCache> m_cache;
    QPoint m_origin;
    QImage m_hqImage;
    mutable QAtomicInt m_cancelFlag;
  };

  std::shared_ptr<Result> m_result;
  QImage m_image;
  QTransform m_xform;
  QSize m_targetSize;
};

HqImageCache::HqImageCache(const QImage& image,
                           BackgroundExecutor& executor,
                           RenderRequestProvider requestProvider,
                           QObject* parent)
    : QObject(parent), m_image(image), m_executor(executor), m_requestProvider(std::move(requestProvider)) {
  m_timer.setSingleShot(true);
  m_timer.setInterval(150);  // msec
  connect(&m_timer, &QTimer::timeout, this, [this]() {
    if (m_requestProvider) {
      buildNow(m_requestProvider());
    }
  });
}

HqImageCache::~HqImageCache() {
  m_timer.stop();
  cancelPending();
}

bool HqImageCache::isValid(const RenderRequest& req) const {
  if (m_hqPixmap.isNull()) {
    return false;
  }

  if (m_hqSourceId != m_image.cacheKey()) {
    return false;
  }

  if (m_hqXform != req.imageToWidget) {
    return false;
  }

  if (m_hqDevicePixelRatio != req.devicePixelRatio) {
    return false;
  }

  if (m_hqTargetSize != req.targetSize) {
    return false;
  }
  return true;
}

bool HqImageCache::canBuild(const RenderRequest& req) const {
  if (m_image.isNull()) {
    return false;
  }

  if (req.targetSize.isEmpty()) {
    return false;
  }

  const QTransform xform(req.imageToWidget * QTransform::fromScale(req.devicePixelRatio, req.devicePixelRatio));
  const QRect targetRect(
      xform.map(QRectF(m_image.rect())).boundingRect().toRect().intersected(QRect(QPoint(0, 0), req.targetSize)));
  return !targetRect.isEmpty();
}

void HqImageCache::scheduleRebuild(const RenderRequest& req) {
  if (!canBuild(req)) {
    cancelPending();
    m_timer.stop();
    return;
  }

  if (!m_timer.isActive() || (m_potentialHqXform != req.imageToWidget)) {
    cancelPending();
    m_potentialHqXform = req.imageToWidget;
  }
  m_timer.start();
}

void HqImageCache::buildNow(const RenderRequest& req) {
  if (isValid(req)) {
    return;
  }

  m_hqPixmap = QPixmap();
  cancelPending();

  if (!canBuild(req)) {
    return;
  }

  const QTransform xform(req.imageToWidget);
  // Built in screen pixels, see m_hqDevicePixelRatio.
  const qreal dpr = req.devicePixelRatio;
  const QSize targetSize(req.targetSize);
  const auto task
      = std::make_shared<HqTransformTask>(this, m_image, xform * QTransform::fromScale(dpr, dpr), targetSize);

  m_executor.enqueueTask(task);

  m_hqTransformTask = task;
  m_hqXform = xform;
  m_hqDevicePixelRatio = dpr;
  m_hqTargetSize = targetSize;
  m_hqSourceId = m_image.cacheKey();
}

void HqImageCache::cancelPending() {
  if (m_hqTransformTask) {
    m_hqTransformTask->cancel();
    m_hqTransformTask.reset();
  }
}

QImage HqImageCache::sampleAtWidgetRect(const QTransform& widgetToImage,
                                        const QRect& widgetRect,
                                        const int maxImageSide) const {
  if (m_image.isNull() || widgetRect.isEmpty()) {
    return QImage();
  }
  // The whole image area under the widget rect, as the screen grab without OpenGL sees it,
  // so that the zoom doesn't change which part of the image counts.
  const QRect imageRect = widgetToImage.mapRect(QRectF(widgetRect)).toAlignedRect().intersected(m_image.rect());
  if (imageRect.isEmpty()) {
    return QImage();
  }
  const QImage area(m_image.copy(imageRect));
  if ((area.width() <= maxImageSide) && (area.height() <= maxImageSide)) {
    return area;
  }
  // Zoomed out far: fewer pixels do for the color, and they keep their original colors.
  return area.scaled(maxImageSide, maxImageSide, Qt::KeepAspectRatio, Qt::FastTransformation);
}

void HqImageCache::onHqVersionBuilt(const QPoint& origin, const QImage& image) {
  m_hqTransformTask.reset();
  if (image.isNull()) {
    m_hqPixmap = QPixmap();
    return;
  }

  // The image and origin are in screen pixels.
  m_hqPixmap = QPixmap::fromImage(image);
  m_hqPixmap.setDevicePixelRatio(m_hqDevicePixelRatio);
  m_hqPixmapPos = QPointF(origin) / m_hqDevicePixelRatio;
  emit ready();
}

/*====================== HqImageCache::HqTransformTask =====================*/

HqImageCache::HqTransformTask::HqTransformTask(HqImageCache* cache,
                                               const QImage& image,
                                               const QTransform& xform,
                                               const QSize& targetSize)
    : m_result(std::make_shared<Result>(cache)), m_image(image), m_xform(xform), m_targetSize(targetSize) {}

std::shared_ptr<AbstractCommand<void>> HqImageCache::HqTransformTask::operator()() {
  if (isCancelled()) {
    return nullptr;
  }

  const QRect targetRect(
      m_xform.map(QRectF(m_image.rect())).boundingRect().toRect().intersected(QRect(QPoint(0, 0), m_targetSize)));

  QImage hqImage(transform(m_image, m_xform, targetRect, OutsidePixels::assumeWeakColor(Qt::white), QSizeF(0.0, 0.0)));

  // In many cases m_image and therefore hqImage are grayscale with
  // a palette, but given that hqImage will be converted to a QPixmap
  // on the GUI thread, it's better to convert it to RGB as a preparation
  // step while we are still in a background thread.
  hqImage
      = hqImage.convertToFormat(hqImage.hasAlphaChannel() ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);

  m_result->setData(targetRect.topLeft(), hqImage);
  return m_result;
}

/*================== HqImageCache::HqTransformTask::Result =================*/

HqImageCache::HqTransformTask::Result::Result(HqImageCache* cache) : m_cache(cache) {}

void HqImageCache::HqTransformTask::Result::setData(const QPoint& origin, const QImage& hqImage) {
  m_hqImage = hqImage;
  m_origin = origin;
}

void HqImageCache::HqTransformTask::Result::operator()() {
  if (m_cache && !isCancelled()) {
    m_cache->onHqVersionBuilt(m_origin, m_hqImage);
  }
}
