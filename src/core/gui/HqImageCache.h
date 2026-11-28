// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_HQIMAGECACHE_H_
#define SCANTAILOR_CORE_HQIMAGECACHE_H_

#include <QImage>
#include <QObject>
#include <QPixmap>
#include <QPointF>
#include <QRect>
#include <QSize>
#include <QTimer>
#include <QTransform>
#include <QtGlobal>
#include <functional>
#include <memory>

class BackgroundExecutor;
class QPoint;

/**
 * \brief Asynchronous high-quality transformed image cache and pixel sampler.
 */
class HqImageCache : public QObject {
  Q_OBJECT
 public:
  struct RenderRequest {
    QTransform imageToWidget;
    qreal devicePixelRatio = 1.0;
    QSize targetSize;
  };

  using RenderRequestProvider = std::function<RenderRequest()>;

  HqImageCache(const QImage& image,
               BackgroundExecutor& executor,
               RenderRequestProvider requestProvider,
               QObject* parent = nullptr);

  ~HqImageCache() override;

  const QImage& image() const { return m_image; }

  bool isValid(const RenderRequest& req) const;

  bool canBuild(const RenderRequest& req) const;

  void scheduleRebuild(const RenderRequest& req);

  void buildNow(const RenderRequest& req);

  void cancelPending();

  const QPixmap& pixmap() const { return m_hqPixmap; }

  const QPointF& pixmapPos() const { return m_hqPixmapPos; }

  QImage sampleAtWidgetRect(const QTransform& widgetToImage, const QRect& widgetRect, int maxImageSide = 31) const;

 signals:
  void ready();

 private:
  class HqTransformTask;

  void onHqVersionBuilt(const QPoint& origin, const QImage& image);

  QImage m_image;
  QTimer m_timer;
  QPixmap m_hqPixmap;
  QPointF m_hqPixmapPos;
  QTransform m_hqXform;
  qreal m_hqDevicePixelRatio = 1.0;
  QSize m_hqTargetSize;
  QTransform m_potentialHqXform;
  qint64 m_hqSourceId = 0;
  std::shared_ptr<HqTransformTask> m_hqTransformTask;
  BackgroundExecutor& m_executor;
  RenderRequestProvider m_requestProvider;
};

#endif  // ifndef SCANTAILOR_CORE_HQIMAGECACHE_H_
