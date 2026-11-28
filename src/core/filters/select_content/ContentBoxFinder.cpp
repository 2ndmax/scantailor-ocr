// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ContentBoxFinder.h"

#include <Binarize.h>
#include <BinaryImage.h>
#include <ConnComp.h>
#include <ConnCompEraserExt.h>
#include <Connectivity.h>
#include <ConnectivityMap.h>
#include <GrayRasterOp.h>
#include <InfluenceMap.h>
#include <MaxWhitespaceFinder.h>
#include <Morphology.h>
#include <PolygonRasterizer.h>
#include <RasterOp.h>
#include <SEDM.h>
#include <SeedFill.h>
#include <SlicedHistogram.h>
#include <Transform.h>

#include <QDebug>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <optional>
#include <queue>
#include <utility>

#include "DebugImages.h"
#include "Despeckle.h"
#include "FilterData.h"
#include "TaskStatus.h"

namespace select_content {
using namespace imageproc;

class ContentBoxFinder::Garbage {
 public:
  enum Type { HOR, VERT };

  Garbage(Type type, const BinaryImage& garbage);

  void add(const BinaryImage& garbage, const QRect& rect);

  const BinaryImage& image() const { return m_garbage; }

  const SEDM& sedm();

 private:
  imageproc::BinaryImage m_garbage;
  SEDM m_sedm;
  SEDM::Borders m_sedmBorders;
  bool m_sedmUpdatePending;
};


namespace {
// 64 bit: already a whole A4 page at 150 dpi gives 1240 * 1240 * 1754 > 2^31.
struct PreferHorizontal {
  bool operator()(const QRect& lhs, const QRect& rhs) const {
    return int64_t(lhs.width()) * lhs.width() * lhs.height() < int64_t(rhs.width()) * rhs.width() * rhs.height();
  }
};

struct PreferVertical {
  bool operator()(const QRect& lhs, const QRect& rhs) const {
    return int64_t(lhs.width()) * lhs.height() * lhs.height() < int64_t(rhs.width()) * rhs.height() * rhs.height();
  }
};

BinaryImage prepareBinarizedPage150(const FilterData& data,
                                    const ImageTransformation& xform150dpi,
                                    const QRectF& pageRect,
                                    const DetectionOptions& options,
                                    DebugImages* const dbg) {
  const GrayImage dataGrayImage = data.grayImageBlackOnWhite();
  const uint8_t darkestGrayLevel = imageproc::darkestGrayLevel(dataGrayImage);
  const QColor outsideColor(darkestGrayLevel, darkestGrayLevel, darkestGrayLevel);

  QImage gray150(transformToGray(dataGrayImage, xform150dpi.transform(), xform150dpi.resultingRect().toRect(),
                                 OutsidePixels::assumeColor(outsideColor)));
  // Note that we fill new areas that appear as a result of
  // rotation with black, not white. Filling them with white
  // may be bad for detecting the shadow around the page.
  if (dbg) {
    dbg->add(gray150, "gray150");
  }

  // As in ScanTailor Experimental: BlurDiv evens out the paper first, so shadows at the spine and
  // yellowed edges are less often taken for content than with the Wolf threshold used before.
  BinaryImage bw150(binarizeEdgeDiv(gray150, QSize(51, 51), 0.0, 0.75));
  if (dbg) {
    dbg->add(bw150, "bw150");
  }

  // Text touching the masked area would be removed with the border, see DetectionOptions.
  if (!options.ignorePageBox()) {
    // pageRect is in the coordinates of data.xform(), whose DPI is already evened out,
    // so it goes back to the original image and from there to the 150 dpi one.
    const QPolygonF pageArea150((data.xform().transformBack() * xform150dpi.transform()).map(QPolygonF(pageRect)));
    PolygonRasterizer::fillExcept(bw150, BLACK, pageArea150, Qt::WindingFill);
  }

  PolygonRasterizer::fillExcept(bw150, BLACK, xform150dpi.resultingPreCropArea(), Qt::WindingFill);
  if (dbg) {
    dbg->add(bw150, "page_mask_applied");
  }
  return bw150;
}

template <typename Preference, typename ShrinkFn>
void markWhitespaceObstacles(const BinaryImage& despeckled,
                             BinaryImage& contentBlocks,
                             const int maxIterations,
                             const int areaThreshold,
                             ShrinkFn shrinkObstacle) {
  MaxWhitespaceFinder wsFinder(Preference(), despeckled);

  for (int i = 0; i < maxIterations; ++i) {
    QRect ws(wsFinder.next(wsFinder.MANUAL_OBSTACLES));
    if (ws.isNull() || (static_cast<int64_t>(ws.width()) * ws.height() < areaThreshold)) {
      break;
    }
    contentBlocks.fill(ws, WHITE);
    shrinkObstacle(ws);
    wsFinder.addObstacle(ws);
  }
}

void markAdditionalWhitespace(const BinaryImage& content, BinaryImage& contentBlocks, const int areaThreshold) {
  BinaryImage tmp(content);
  rasterOp<RopOr<RopNot<RopSrc>, RopDst>>(tmp, contentBlocks);
  MaxWhitespaceFinder wsFinder(tmp.release(), QSize(4, 4));

  for (int i = 0; i < 10; ++i) {
    const QRect ws(wsFinder.next());
    if (ws.isNull() || (static_cast<int64_t>(ws.width()) * ws.height() < areaThreshold)) {
      break;
    }
    contentBlocks.fill(ws, WHITE);
  }
}

QImage visualizeTextMask(const BinaryImage& content, const BinaryImage& textMask) {
  QImage textMaskVisualized(content.size(), QImage::Format_ARGB32_Premultiplied);
  textMaskVisualized.fill(0xffffffff);  // Opaque white.
  QPainter painter(&textMaskVisualized);

  QImage tmp(content.size(), QImage::Format_ARGB32_Premultiplied);
  tmp.fill(0xff64dd62);  // Opaque light green.
  tmp.setAlphaChannel(textMask.inverted().toQImage());
  painter.drawImage(QPoint(0, 0), tmp);

  tmp.fill(0xe0000000);  // Mostly transparent black.
  tmp.setAlphaChannel(content.inverted().toQImage());
  painter.drawImage(QPoint(0, 0), tmp);

  painter.end();
  return textMaskVisualized;
}

bool isDegenerateContentRect(const QRect& rect) {
  return (rect.width() < 8) || (rect.height() < 8) || ((rect.width() < 30) && (rect.height() > rect.width() * 20));
}
}  // namespace

QRectF ContentBoxFinder::findContentBox(const TaskStatus& status,
                                        const FilterData& data,
                                        const QRectF& pageRect,
                                        const DetectionOptions& options,
                                        DebugImages* dbg) {
  // Adopted from ScanTailor Spectre: from 95 % on, everything inside the page box counts as
  // content (for art books and photo pages).
  if (options.fillFactor() >= 95) {
    return pageRect;
  }
  const double maxFillFactor = options.fillFactor() / 100.0;

  ImageTransformation xform150dpi(data.xform());
  xform150dpi.preScaleToDpi(Dpi(150, 150));

  if (xform150dpi.resultingRect().toRect().isEmpty()) {
    return QRectF();
  }

  BinaryImage bw150(prepareBinarizedPage150(data, xform150dpi, pageRect, options, dbg));
  BinaryImage garbage(detectShadows(status, bw150, dbg));

  BinaryImage content(bw150.release());
  // Above 85 %, shadows are not subtracted, so large dark areas such as photo backgrounds stay.
  if (options.fillFactor() <= 85) {
    rasterOp<RopSubtract<RopDst, RopSrc>>(content, garbage);
  }
  if (dbg) {
    dbg->add(content, "content");
  }

  status.throwIfCancelled();

  Despeckle::Level despeckleLevel = Despeckle::NORMAL;

  BinaryImage despeckled(Despeckle::despeckle(content, Dpi(150, 150), despeckleLevel, status, dbg));
  if (dbg) {
    dbg->add(despeckled, "despeckled");
  }

  status.throwIfCancelled();

  const BinaryImage contentBlocks(buildContentBlocks(content, despeckled, options.borderTolerance(), dbg));

  BinaryImage textMask(estimateTextMask(status, content, contentBlocks, maxFillFactor, dbg));
  if (dbg) {
    dbg->add(visualizeTextMask(content, textMask), "textMask");
  }

  // Make textMask strore the actual content pixels that are text.
  rasterOp<RopAnd<RopSrc, RopDst>>(textMask, content);

  BinaryImage horGarbageImg;
  BinaryImage vertGarbageImg;
  segmentGarbage(garbage, horGarbageImg, vertGarbageImg, dbg);
  garbage.release();

  if (dbg) {
    dbg->add(horGarbageImg, "initial_hor_garbage");
    dbg->add(vertGarbageImg, "initial_vert_garbage");
  }

  Garbage horGarbage(Garbage::HOR, horGarbageImg.release());
  Garbage vertGarbage(Garbage::VERT, vertGarbageImg.release());

  QRect contentRect(trimContentRect(status, content, contentBlocks, textMask, contentBlocks.contentBoundingBox(),
                                    horGarbage, vertGarbage, dbg));

  // Increase the content rect due to cutting off the content at edges because of rescaling made.
  if (!contentRect.isEmpty()) {
    contentRect.adjust(-1, -1, 1, 1);
  }

  // Transform back from 150dpi.
  QTransform combinedXform(xform150dpi.transform().inverted());
  combinedXform *= data.xform().transform();
  return combinedXform.map(QRectF(contentRect)).boundingRect().intersected(data.xform().resultingRect());
}  // ContentBoxFinder::findContentBox

imageproc::BinaryImage ContentBoxFinder::detectShadows(const TaskStatus& status,
                                                       const imageproc::BinaryImage& bw150,
                                                       DebugImages* const dbg) {
  BinaryImage horShadowsSeed(openBrick(bw150, QSize(200, 14), BLACK));
  if (dbg) {
    dbg->add(horShadowsSeed, "horShadowsSeed");
  }

  status.throwIfCancelled();

  BinaryImage verShadowsSeed(openBrick(bw150, QSize(14, 300), BLACK));
  if (dbg) {
    dbg->add(verShadowsSeed, "verShadowsSeed");
  }

  status.throwIfCancelled();

  BinaryImage shadowsSeed(horShadowsSeed.release());
  rasterOp<RopOr<RopSrc, RopDst>>(shadowsSeed, verShadowsSeed);
  verShadowsSeed.release();
  if (dbg) {
    dbg->add(shadowsSeed, "shadowsSeed");
  }

  status.throwIfCancelled();

  BinaryImage dilated(dilateBrick(bw150, QSize(3, 3)));
  if (dbg) {
    dbg->add(dilated, "dilated");
  }

  status.throwIfCancelled();

  BinaryImage shadowsDilated(seedFill(shadowsSeed, dilated, CONN8));
  dilated.release();
  if (dbg) {
    dbg->add(shadowsDilated, "shadowsDilated");
  }

  status.throwIfCancelled();

  rasterOp<RopAnd<RopSrc, RopDst>>(shadowsDilated, bw150);
  BinaryImage garbage(shadowsDilated.release());
  if (dbg) {
    dbg->add(garbage, "shadows");
  }

  status.throwIfCancelled();

  filterShadows(status, garbage, dbg);
  if (dbg) {
    dbg->add(garbage, "filtered_shadows");
  }

  status.throwIfCancelled();
  return garbage;
}

imageproc::BinaryImage ContentBoxFinder::buildContentBlocks(const imageproc::BinaryImage& content,
                                                            imageproc::BinaryImage& despeckled,
                                                            const int borderTolerance,
                                                            DebugImages* const dbg) {
  BinaryImage contentBlocks(content.size(), BLACK);
  const int areaThreshold = std::min(content.width(), content.height());

  markWhitespaceObstacles<PreferHorizontal>(despeckled, contentBlocks, 80, areaThreshold, [](QRect& ws) {
    const int heightFraction = ws.height() / 5;
    ws.setTop(ws.top() + heightFraction);
    ws.setBottom(ws.bottom() - heightFraction);
  });
  markWhitespaceObstacles<PreferVertical>(despeckled, contentBlocks, 40, areaThreshold, [](QRect& ws) {
    const int widthFraction = ws.width() / 5;
    ws.setLeft(ws.left() + widthFraction);
    ws.setRight(ws.right() - widthFraction);
  });

  if (dbg) {
    dbg->add(contentBlocks, "contentBlocks");
  }

  trimContentBlocksInPlace(despeckled, contentBlocks);
  if (dbg) {
    dbg->add(contentBlocks, "initial_trimming");
  }

  // Do some more whitespace finding. This should help us separate
  // blocks that don't belong together.
  markAdditionalWhitespace(content, contentBlocks, areaThreshold);
  if (dbg) {
    dbg->add(contentBlocks, "more_whitespace");
  }

  trimContentBlocksInPlace(despeckled, contentBlocks);
  if (dbg) {
    dbg->add(contentBlocks, "more_trimming");
  }

  despeckled.release();

  inPlaceRemoveAreasTouchingBorders(contentBlocks, borderTolerance, dbg);
  if (dbg) {
    dbg->add(contentBlocks, "except_bordering");
  }

  return contentBlocks;
}

QRect ContentBoxFinder::trimContentRect(const TaskStatus& status,
                                        const imageproc::BinaryImage& content,
                                        const imageproc::BinaryImage& contentBlocks,
                                        const imageproc::BinaryImage& textMask,
                                        QRect contentRect,
                                        Garbage& horGarbage,
                                        Garbage& vertGarbage,
                                        DebugImages* const dbg) {
  enum Side { LEFT = 1, RIGHT = 2, TOP = 4, BOTTOM = 8 };
  struct EdgeSpec {
    Side side;
    Qt::Edge edge;
    int retrySides;
    Garbage& garbage;
  };
  const EdgeSpec specs[] = {
      {LEFT, Qt::LeftEdge, LEFT | TOP | BOTTOM, vertGarbage},
      {RIGHT, Qt::RightEdge, RIGHT | TOP | BOTTOM, vertGarbage},
      {TOP, Qt::TopEdge, TOP | LEFT | RIGHT, horGarbage},
      {BOTTOM, Qt::BottomEdge, BOTTOM | LEFT | RIGHT, horGarbage},
  };

  int sideMask = LEFT | RIGHT | TOP | BOTTOM;
  while (sideMask && !contentRect.isEmpty()) {
    for (const EdgeSpec& spec : specs) {
      if (!(sideMask & spec.side)) {
        continue;
      }
      sideMask &= ~spec.side;
      const QRect oldContentRect = contentRect;
      contentRect = trimEdge(content, contentBlocks, textMask, contentRect, spec.edge, spec.garbage, dbg);
      status.throwIfCancelled();
      if (contentRect.isEmpty()) {
        break;
      }
      if (oldContentRect != contentRect) {
        sideMask |= spec.retrySides;
      }
    }

    if (!contentRect.isEmpty() && isDegenerateContentRect(contentRect)) {
      contentRect = QRect();
      break;
    }
  }
  return contentRect;
}

namespace {
struct Bounds {
  // All are inclusive.
  int left;
  int right;
  int top;
  int bottom;

  Bounds() : left(INT_MAX), right(INT_MIN), top(INT_MAX), bottom(INT_MIN) {}

  bool isInside(int x, int y) const { return (x >= left) && (x <= right) && (y >= top) && (y <= bottom); }

  void forceInside(int x, int y) {
    if (x < left) {
      left = x;
    }
    if (x > right) {
      right = x;
    }
    if (y < top) {
      top = y;
    }
    if (y > bottom) {
      bottom = y;
    }
  }
};
}  // namespace

void ContentBoxFinder::trimContentBlocksInPlace(const imageproc::BinaryImage& content,
                                                imageproc::BinaryImage& contentBlocks) {
  const ConnectivityMap cmap(contentBlocks, CONN4);
  std::vector<Bounds> bounds(cmap.maxLabel() + 1);

  int width = content.width();
  int height = content.height();
  const uint32_t msb = uint32_t(1) << 31;

  const uint32_t* contentLine = content.data();
  const int contentStride = content.wordsPerLine();
  const uint32_t* cmapLine = cmap.data();
  const int cmapStride = cmap.stride();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t label = cmapLine[x];
      if (label == 0) {
        continue;
      }
      if (contentLine[x >> 5] & (msb >> (x & 31))) {
        bounds[label].forceInside(x, y);
      }
    }
    cmapLine += cmapStride;
    contentLine += contentStride;
  }

  uint32_t* cbLine = contentBlocks.data();
  const int cbStride = contentBlocks.wordsPerLine();
  cmapLine = cmap.data();
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t label = cmapLine[x];
      if (label == 0) {
        continue;
      }
      if (!bounds[label].isInside(x, y)) {
        cbLine[x >> 5] &= ~(msb >> (x & 31));
      }
    }
    cmapLine += cmapStride;
    cbLine += cbStride;
  }
}  // ContentBoxFinder::trimContentBlocksInPlace

namespace {
void seedBorderCell(uint16_t* const cell, const uint16_t maxSpreadDist, std::queue<uint16_t*>& queue) {
  if (*cell == 0) {
    *cell = maxSpreadDist;
    queue.push(cell);
  }
}

void seedBorderPixels(std::vector<uint16_t>& map,
                      const int width,
                      const int height,
                      const uint16_t maxSpreadDist,
                      std::queue<uint16_t*>& queue) {
  const int mapStride = width + 2;
  uint16_t* mapLine = map.data() + mapStride + 1;
  for (int x = 0; x < width; ++x) {
    seedBorderCell(&mapLine[x], maxSpreadDist, queue);
  }
  mapLine += mapStride;
  for (int y = 1; y < height - 1; ++y) {
    seedBorderCell(&mapLine[0], maxSpreadDist, queue);
    seedBorderCell(&mapLine[width - 1], maxSpreadDist, queue);
    mapLine += mapStride;
  }
  for (int x = 0; x < width; ++x) {
    seedBorderCell(&mapLine[x], maxSpreadDist, queue);
  }
}

void relaxBorderNeighbor(uint16_t* const nbh, const uint16_t newDist, std::queue<uint16_t*>& queue) {
  if (newDist > *nbh) {
    *nbh = newDist;
    queue.push(nbh);
  }
}

void spreadBorderDistances(std::queue<uint16_t*>& queue, const int mapStride) {
  while (!queue.empty()) {
    uint16_t* const cell = queue.front();
    queue.pop();

    assert(*cell != 0);
    const auto newDist = static_cast<uint16_t>(*cell - 1);

    relaxBorderNeighbor(cell - mapStride, newDist, queue);
    relaxBorderNeighbor(cell - 1, newDist, queue);
    relaxBorderNeighbor(cell + 1, newDist, queue);
    relaxBorderNeighbor(cell + mapStride, newDist, queue);
  }
}

void clearReachedBorderPixels(imageproc::BinaryImage& contentBlocks, const std::vector<uint16_t>& map) {
  const int width = contentBlocks.width();
  const int height = contentBlocks.height();
  const int mapStride = width + 2;
  uint32_t* cbLine = contentBlocks.data();
  const int cbStride = contentBlocks.wordsPerLine();
  const uint16_t* mapLine = map.data() + mapStride + 1;
  const uint32_t msb = uint32_t(1) << 31;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      // Reached by the spread from the border: neither unreached black (0) nor white (max).
      if ((mapLine[x] != 0) && (mapLine[x] != std::numeric_limits<uint16_t>::max())) {
        cbLine[x >> 5] &= ~(msb >> (x & 31));
      }
    }
    mapLine += mapStride;
    cbLine += cbStride;
  }
}
}  // namespace

void ContentBoxFinder::inPlaceRemoveAreasTouchingBorders(imageproc::BinaryImage& contentBlocks,
                                                         const int borderTolerance,
                                                         DebugImages* /*dbg*/) {
  // We could just do a seed fill from borders, but that
  // has the potential to remove too much. Instead, we
  // do something similar to a seed fill, but with a limited
  // spread distance.

  const int width = contentBlocks.width();
  const int height = contentBlocks.height();
  if ((width <= 0) || (height <= 0)) {
    return;
  }

  // The border tolerance (0..100 %) shortens the spread distance: 0 keeps the full distance,
  // 100 removes only the pixels on the border itself. Enforce >= 1 so border pixels never push 0
  // and wrap uint16_t(*cell - 1) on narrow crops.
  const int fullSpreadDist = std::max(1, std::min(width, height) / 4);
  const int spreadDist = (borderTolerance <= 0)
                             ? fullSpreadDist
                             : std::max(1, fullSpreadDist * (DetectionOptions::MAX_BORDER_TOLERANCE - borderTolerance)
                                               / DetectionOptions::MAX_BORDER_TOLERANCE);
  const auto maxSpreadDist = static_cast<uint16_t>(std::min<int>(spreadDist, std::numeric_limits<uint16_t>::max() - 1));

  const int mapStride = width + 2;
  std::vector<uint16_t> map(mapStride * (height + 2), std::numeric_limits<uint16_t>::max());

  const uint32_t* cbLine = contentBlocks.data();
  const int cbStride = contentBlocks.wordsPerLine();
  uint16_t* mapLine = map.data() + mapStride + 1;
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint32_t mask = cbLine[x >> 5] >> (31 - (x & 31));
      mask &= uint32_t(1);
      --mask;

      // WHITE -> max, BLACK -> 0
      mapLine[x] = static_cast<uint16_t>(mask);
    }
    mapLine += mapStride;
    cbLine += cbStride;
  }

  std::queue<uint16_t*> queue;
  seedBorderPixels(map, width, height, maxSpreadDist, queue);

  if (queue.empty()) {
    // Common case optimization.
    return;
  }

  spreadBorderDistances(queue, mapStride);
  clearReachedBorderPixels(contentBlocks, map);
}  // ContentBoxFinder::inPlaceRemoveAreasTouchingBorders

void ContentBoxFinder::segmentGarbage(const imageproc::BinaryImage& garbage,
                                      imageproc::BinaryImage& horGarbage,
                                      imageproc::BinaryImage& vertGarbage,
                                      DebugImages* /*dbg*/) {
  horGarbage = openBrick(garbage, QSize(200, 1), WHITE);

  QRect rect(garbage.rect());
  rect.setHeight(1);
  rasterOp<RopOr<RopSrc, RopDst>>(horGarbage, rect, garbage, rect.topLeft());
  rect.moveBottom(garbage.rect().bottom());
  rasterOp<RopOr<RopSrc, RopDst>>(horGarbage, rect, garbage, rect.topLeft());

  vertGarbage = openBrick(garbage, QSize(1, 200), WHITE);

  rect = garbage.rect();
  rect.setWidth(1);
  rasterOp<RopOr<RopSrc, RopDst>>(vertGarbage, rect, garbage, rect.topLeft());
  rect.moveRight(garbage.rect().right());
  rasterOp<RopOr<RopSrc, RopDst>>(vertGarbage, rect, garbage, rect.topLeft());

  ConnectivityMap cmap(garbage.size());

  cmap.addComponent(vertGarbage);
  vertGarbage.fill(WHITE);
  cmap.addComponent(horGarbage);
  horGarbage.fill(WHITE);

  InfluenceMap imap(cmap, garbage);
  cmap = ConnectivityMap();

  const int width = garbage.width();
  const int height = garbage.height();

  InfluenceMap::Cell* imapLine = imap.data();
  const int imapStride = imap.stride();

  uint32_t* vgLine = vertGarbage.data();
  const int vgStride = vertGarbage.wordsPerLine();
  uint32_t* hgLine = horGarbage.data();
  const int hgStride = horGarbage.wordsPerLine();

  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      switch (imapLine[x].label) {
        case 1:
          vgLine[x >> 5] |= msb >> (x & 31);
          break;
        case 2:
          hgLine[x >> 5] |= msb >> (x & 31);
          break;
        default:
          break;
      }
    }
    imapLine += imapStride;
    vgLine += vgStride;
    hgLine += hgStride;
  }

  BinaryImage unconnectedGarbage(garbage);
  rasterOp<RopSubtract<RopDst, RopSrc>>(unconnectedGarbage, horGarbage);
  rasterOp<RopSubtract<RopDst, RopSrc>>(unconnectedGarbage, vertGarbage);

  rasterOp<RopOr<RopSrc, RopDst>>(horGarbage, unconnectedGarbage);
  rasterOp<RopOr<RopSrc, RopDst>>(vertGarbage, unconnectedGarbage);
}  // ContentBoxFinder::segmentGarbage

namespace {
using HistogramRange = std::pair<const int*, const int*>;

struct LineWindow {
  int top;
  int bottom;
  int numBlack;
  int numTotal;
  int maxWidth;
};

QImage visualizeUeps(const BinaryImage& content, const BinaryImage& contentBlocks, const BinaryImage& ueps) {
  QImage canvas(contentBlocks.toQImage().convertToFormat(QImage::Format_ARGB32_Premultiplied));
  QPainter painter;
  painter.begin(&canvas);
  QImage overlay(canvas.size(), canvas.format());
  overlay.fill(0xff0000ff);  // opaque blue
  overlay.setAlphaChannel(content.inverted().toQImage());
  painter.drawImage(QPoint(0, 0), overlay);

  BinaryImage uepsOnContentBlocks(contentBlocks);
  rasterOp<RopAnd<RopSrc, RopDst>>(uepsOnContentBlocks, ueps);

  overlay.fill(0xffffff00);  // opaque yellow
  overlay.setAlphaChannel(uepsOnContentBlocks.inverted().toQImage());
  painter.drawImage(QPoint(0, 0), overlay);

  painter.end();
  return canvas;
}

void fillPeakEnvelopes(const int* const first,
                       const int* const last,
                       std::vector<int>& maxForward,
                       std::vector<int>& maxBackwards) {
  int prev = *first;
  for (int i = 0; i <= last - first; ++i) {
    prev = std::max(prev, first[i]);
    maxForward[i] = prev;
  }
  prev = *last;
  for (int i = 0; i <= last - first; ++i) {
    prev = std::max(prev, last[-i]);
    maxBackwards[i] = prev;
  }
}

const int* findBestLineSplitPos(const int* const first,
                                const int* const last,
                                std::vector<int>& maxForward,
                                std::vector<int>& maxBackwards) {
  fillPeakEnvelopes(first, last, maxForward, maxBackwards);

  int bestMagnitude = std::numeric_limits<int>::min();
  const int* bestSplitPos = nullptr;
  assert(first != last);
  for (const int* p = first + 1; p != last; ++p) {
    const int peak1 = maxForward[p - (first + 1)];
    const int peak2 = maxBackwards[(last - 1) - p];
    if (*p * 3.5 > 0.5 * (peak1 + peak2)) {
      continue;
    }
    const int shoulder1 = peak1 - *p;
    const int shoulder2 = peak2 - *p;
    if ((shoulder1 <= 0) || (shoulder2 <= 0)) {
      continue;
    }
    if (std::min(shoulder1, shoulder2) * 20 < std::max(shoulder1, shoulder2)) {
      continue;
    }

    const int magnitude = shoulder1 + shoulder2;
    if (magnitude > bestMagnitude) {
      bestMagnitude = magnitude;
      bestSplitPos = p;
    }
  }
  return bestSplitPos;
}

std::vector<HistogramRange> splitTextLineRanges(const SlicedHistogram& hist, const int minTextHeight) {
  std::vector<HistogramRange> ranges;
  std::vector<HistogramRange> splittableRanges;
  splittableRanges.emplace_back(&hist[0], &hist[hist.size() - 1]);

  std::vector<int> maxForward(hist.size());
  std::vector<int> maxBackwards(hist.size());

  // Try splitting text lines.
  while (!splittableRanges.empty()) {
    const int* const first = splittableRanges.back().first;
    const int* const last = splittableRanges.back().second;
    splittableRanges.pop_back();

    if (last - first < minTextHeight - 1) {
      // Just ignore such a small segment.
      continue;
    }

    const int* const bestSplitPos = findBestLineSplitPos(first, last, maxForward, maxBackwards);
    if (bestSplitPos) {
      splittableRanges.emplace_back(first, bestSplitPos - 1);
      splittableRanges.emplace_back(bestSplitPos + 1, last);
    } else {
      ranges.emplace_back(first, last);
    }
  }
  return ranges;
}

std::optional<LineWindow> findInitialLineWindow(const SlicedHistogram& hist,
                                                const SlicedHistogram& blockHist,
                                                const int first,
                                                const int last,
                                                const int minTextHeight,
                                                const double minFillFactor,
                                                const double maxFillFactor,
                                                int& outCenterY) {
  int64_t weightedY = 0;
  int totalWeight = 0;
  for (int i = first; i <= last; ++i) {
    const int val = hist[i];
    weightedY += val * i;
    totalWeight += val;
  }

  if (totalWeight == 0) {
    // qDebug() << "no black pixels at all";
    return std::nullopt;
  }

  outCenterY = static_cast<int>((weightedY + totalWeight / 2) / totalWeight);
  const int top = outCenterY - minTextHeight / 2;
  const int bottom = top + minTextHeight - 1;
  if ((top < first) || (bottom > last)) {
    return std::nullopt;
  }

  LineWindow win{top, bottom, 0, 0, 0};
  for (int i = top; i <= bottom; ++i) {
    win.numBlack += hist[i];
    win.numTotal += blockHist[i];
    win.maxWidth = std::max(win.maxWidth, blockHist[i]);
  }
  if ((win.numBlack < win.numTotal * minFillFactor) || (win.numBlack > win.numTotal * maxFillFactor)) {
    return std::nullopt;
  }
  return win;
}

void extendLineWindow(const SlicedHistogram& hist,
                      const SlicedHistogram& blockHist,
                      const int first,
                      const int last,
                      const int centerY,
                      const double minFillFactor,
                      LineWindow& win) {
  // Extend the top and bottom of the text line.
  while ((win.top > first || win.bottom < last) && std::abs((centerY - win.top) - (win.bottom - centerY)) <= 1) {
    int newTop = win.top;
    if (win.top > first) {
      --newTop;
      win.numBlack += hist[newTop];
      win.numTotal += blockHist[newTop];
    }
    int newBottom = win.bottom;
    if (win.bottom < last) {
      ++newBottom;
      win.numBlack += hist[newBottom];
      win.numTotal += blockHist[newBottom];
    }
    if (win.numBlack < win.numTotal * minFillFactor) {
      break;
    }
    win.maxWidth = std::max(win.maxWidth, blockHist[newTop]);
    win.maxWidth = std::max(win.maxWidth, blockHist[newBottom]);
    win.top = newTop;
    win.bottom = newBottom;
  }
}

bool hasEnoughLineUeps(const BinaryImage& contentBlocks,
                       const BinaryImage& ueps,
                       const QRect& lineRect,
                       const double maxFillFactor) {
  // Check if there are enough ultimate eroded points on the line.
  // Pictures have few of them, so above a fill factor of 85 % fewer are required
  // (adopted from ScanTailor Spectre).
  const double uepFactor = (maxFillFactor > 0.85) ? 0.1 : 0.4;
  auto uepsTodo = int(uepFactor * lineRect.width() / lineRect.height());
  if (!uepsTodo) {
    return true;
  }

  BinaryImage lineUeps(lineRect.size());
  rasterOp<RopSrc>(lineUeps, lineUeps.rect(), contentBlocks, lineRect.topLeft());
  rasterOp<RopAnd<RopSrc, RopDst>>(lineUeps, lineUeps.rect(), ueps, lineRect.topLeft());
  ConnCompEraser uepsEraser(lineUeps, CONN4);
  for (; uepsTodo && !uepsEraser.nextConnComp().isNull(); --uepsTodo) {
    // Erase components until uepsTodo reaches zero or there are no more components.
  }
  return uepsTodo == 0;
}

void processTextComponent(const BinaryImage& content,
                          const BinaryImage& contentBlocks,
                          const BinaryImage& ueps,
                          const ConnComp& cc,
                          const BinaryImage& ccImg,
                          const double maxFillFactor,
                          BinaryImage& textMask) {
  BinaryImage contentImg(ccImg.size());
  rasterOp<RopSrc>(contentImg, contentImg.rect(), content, cc.rect().topLeft());

  // Note that some content may actually be not masked
  // by contentBlocks, because we build contentBlocks
  // based on despeckled content image.
  rasterOp<RopAnd<RopSrc, RopDst>>(contentImg, ccImg);

  const SlicedHistogram hist(contentImg, SlicedHistogram::ROWS);
  const SlicedHistogram blockHist(ccImg, SlicedHistogram::ROWS);

  assert(hist.size() != 0);

  const int minTextHeight = 6;
  const double minFillFactor = 0.22;
  const std::vector<HistogramRange> ranges(splitTextLineRanges(hist, minTextHeight));

  for (const HistogramRange& range : ranges) {
    const auto first = static_cast<int>(range.first - &hist[0]);
    const auto last = static_cast<int>(range.second - &hist[0]);
    if (last - first < minTextHeight - 1) {
      continue;
    }

    int centerY = 0;
    std::optional<LineWindow> win
        = findInitialLineWindow(hist, blockHist, first, last, minTextHeight, minFillFactor, maxFillFactor, centerY);
    if (!win) {
      continue;
    }

    extendLineWindow(hist, blockHist, first, last, centerY, minFillFactor, *win);

    if ((win->numBlack > win->numTotal * maxFillFactor) || (win->maxWidth < (win->bottom - win->top + 1) * 0.6)) {
      continue;
    }

    QRect lineRect(cc.rect());
    lineRect.setTop(cc.rect().top() + win->top);
    lineRect.setBottom(cc.rect().top() + win->bottom);

    if (!hasEnoughLineUeps(contentBlocks, ueps, lineRect, maxFillFactor)) {
      continue;
    }

    // Write this block to the text mask.
    rasterOp<RopOr<RopSrc, RopDst>>(textMask, lineRect, ccImg, QPoint(0, win->top));
  }
}
}  // namespace

imageproc::BinaryImage ContentBoxFinder::estimateTextMask(const TaskStatus& status,
                                                          const imageproc::BinaryImage& content,
                                                          const imageproc::BinaryImage& contentBlocks,
                                                          const double maxFillFactor,
                                                          DebugImages* dbg) {
  // We differentiate between a text line and a slightly skewed straight
  // line (which may have a fill factor similar to that of text) by the
  // presence of ultimate eroded points.

  const BinaryImage ueps(SEDM(content, SEDM::DIST_TO_BLACK, SEDM::DIST_TO_NO_BORDERS).findPeaksDestructive());
  status.throwIfCancelled();
  if (dbg) {
    dbg->add(visualizeUeps(content, contentBlocks, ueps), "ueps");
  }

  BinaryImage textMask(content.size(), WHITE);

  ConnCompEraserExt eraser(contentBlocks, CONN4);
  while (true) {
    const ConnComp cc(eraser.nextConnComp());
    if (cc.isNull()) {
      break;
    }
    status.throwIfCancelled();

    const BinaryImage ccImg(eraser.computeConnCompImage());
    processTextComponent(content, contentBlocks, ueps, cc, ccImg, maxFillFactor, textMask);
  }
  return textMask;
}  // ContentBoxFinder::estimateTextMask

QRect ContentBoxFinder::trimEdge(const imageproc::BinaryImage& content,
                                 const imageproc::BinaryImage& contentBlocks,
                                 const imageproc::BinaryImage& text,
                                 const QRect& area,
                                 const Qt::Edge edge,
                                 Garbage& garbage,
                                 DebugImages* const dbg) {
  const bool columns = (edge == Qt::LeftEdge) || (edge == Qt::RightEdge);
  const bool fromStart = (edge == Qt::LeftEdge) || (edge == Qt::TopEdge);
  const SlicedHistogram hist(contentBlocks, area, columns ? SlicedHistogram::COLS : SlicedHistogram::ROWS);
  const int size = static_cast<int>(hist.size());
  const int offset = columns ? area.left() : area.top();
  // The direction from the edge into the area.
  const int step = fromStart ? 1 : -1;
  const auto inHist = [size](const int i) { return (i >= 0) && (i < size); };

  int start = fromStart ? 0 : size - 1;
  while (inHist(start)) {
    int firstWs = start;
    for (; inHist(firstWs) && hist[firstWs] != 0; firstWs += step) {
      // Skip non-empty lines.
    }
    int firstNonWs = firstWs;
    for (; inHist(firstNonWs) && hist[firstNonWs] == 0; firstNonWs += step) {
      // Skip empty lines.
    }

    // newArea starts at the content after the gap; removedArea is what lies before the gap.
    const int newAreaEdge = offset + firstNonWs;
    const int removedAreaEnd = offset + firstWs - step;
    QRect newArea(area);
    QRect removedArea(area);
    switch (edge) {
      case Qt::LeftEdge:
        newArea.setLeft(newAreaEdge);
        removedArea.setRight(removedAreaEnd);
        break;
      case Qt::RightEdge:
        newArea.setRight(newAreaEdge);
        removedArea.setLeft(removedAreaEnd);
        break;
      case Qt::TopEdge:
        newArea.setTop(newAreaEdge);
        removedArea.setBottom(removedAreaEnd);
        break;
      case Qt::BottomEdge:
        newArea.setBottom(newAreaEdge);
        removedArea.setTop(removedAreaEnd);
        break;
    }
    if (newArea.isEmpty()) {
      return area;
    }
    if (removedArea.isEmpty()) {
      return newArea;
    }

    bool canRetryGrouped = false;
    const QRect res = trim(content, contentBlocks, text, area, newArea, removedArea, garbage, canRetryGrouped, dbg);
    if (canRetryGrouped) {
      start = firstNonWs;
    } else {
      return res;
    }
  }
  return area;
}  // ContentBoxFinder::trimEdge

namespace {
QImage buildTrimBaseVisualization(const BinaryImage& contentBlocks, const QRect& area) {
  QImage visualized(contentBlocks.size(), QImage::Format_ARGB32_Premultiplied);
  QPainter painter(&visualized);
  painter.drawImage(QPoint(0, 0), contentBlocks.toQImage());

  QPainterPath outerPath;
  outerPath.addRect(visualized.rect());
  QPainterPath innerPath;
  innerPath.addRect(area);

  // Fill already rejected area with translucent gray.
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x00, 0x00, 0x00, 50));
  painter.drawPath(outerPath.subtracted(innerPath));
  return visualized;
}

void addTrimDebugImage(DebugImages* const dbg,
                       QImage& visualized,
                       const QRect& newArea,
                       const QRect& removedArea,
                       const bool trimmed,
                       const char* const label) {
  if (!dbg) {
    return;
  }
  QPainter painter(&visualized);
  painter.setPen(Qt::NoPen);
  painter.setBrush(QColor(0x5f, 0xdf, 0x57, 50));
  painter.drawRect(newArea);
  if (trimmed) {
    painter.setBrush(QColor(0xff, 0x20, 0x1e, 50));
  }
  painter.drawRect(removedArea);
  painter.end();
  dbg->add(visualized, label);
}

bool isTrimTooLarge(const QRect& removedArea, const QRect& newArea) {
  const int64_t removedRectArea = static_cast<int64_t>(removedArea.width()) * removedArea.height();
  const int64_t newRectArea = static_cast<int64_t>(newArea.width()) * newArea.height();
  return (static_cast<double>(removedRectArea) > 0.3 * static_cast<double>(newRectArea)) && (removedArea.width() >= 6)
         && (removedArea.height() >= 6);
}

double computeTrimProximityBias(const BinaryImage& content,
                                const BinaryImage& text,
                                const BinaryImage& garbageImage,
                                const QRect& area,
                                const QRect& newArea,
                                const QRect& removedArea) {
  const int contentPixels = content.countBlackPixels(removedArea);
  const bool verticalCut = (newArea.top() == area.top() && newArea.bottom() == area.bottom());
  // Ranged from 0.0 to 1.0. When it's less than 0.5, objects
  // are more likely to be considered as garbage. When it's
  // more than 0.5, objects are less likely to be considered
  // as garbage.
  double proximityBias = verticalCut ? 0.5 : 0.65;

  const int numTextPixels = text.countBlackPixels(removedArea);
  if (numTextPixels == 0) {
    return verticalCut ? 0.4 : 0.5;
  }

  int totalPixels = contentPixels;
  totalPixels += garbageImage.countBlackPixels(removedArea);
  ++totalPixels;  // just in case

  const double minTextInfluence = 0.2;
  const double maxTextInfluence = 1.0;
  const int upperThreshold = 5000;
  double textInfluence = maxTextInfluence;
  if (numTextPixels < upperThreshold) {
    textInfluence = minTextInfluence
                    + (maxTextInfluence - minTextInfluence) * std::log((double) numTextPixels)
                          / std::log((double) upperThreshold);
  }

  proximityBias += (1.0 - proximityBias) * textInfluence * numTextPixels / totalPixels;
  return qBound(0.0, proximityBias, 1.0);
}

void sumBorderDistances(const BinaryImage& contentBlocks,
                        const SEDM& dmToGarbage,
                        const SEDM& dmToOthers,
                        const QRect& removedArea,
                        double& sumDistToGarbage,
                        double& sumDistToOthers) {
  sumDistToGarbage = 0;
  sumDistToOthers = 0;

  const uint32_t* cbLine = contentBlocks.data();
  const int cbStride = contentBlocks.wordsPerLine();
  const uint32_t msb = uint32_t(1) << 31;

  const uint32_t* dmGarbageLine = dmToGarbage.data();
  const uint32_t* dmOthersLine = dmToOthers.data();
  const int dmStride = dmToOthers.stride();

  cbLine += cbStride * removedArea.top();
  dmGarbageLine += dmStride * removedArea.top();
  dmOthersLine += dmStride * removedArea.top();
  for (int y = removedArea.top(); y <= removedArea.bottom(); ++y) {
    for (int x = removedArea.left(); x <= removedArea.right(); ++x) {
      if (cbLine[x >> 5] & (msb >> (x & 31))) {
        sumDistToGarbage += std::sqrt((double) dmGarbageLine[x]);
        sumDistToOthers += std::sqrt((double) dmOthersLine[x]);
      }
    }
    cbLine += cbStride;
    dmGarbageLine += dmStride;
    dmOthersLine += dmStride;
  }
}
}  // namespace

QRect ContentBoxFinder::trim(const imageproc::BinaryImage& content,
                             const imageproc::BinaryImage& contentBlocks,
                             const imageproc::BinaryImage& text,
                             const QRect& area,
                             const QRect& newArea,
                             const QRect& removedArea,
                             Garbage& garbage,
                             bool& canRetryGrouped,
                             DebugImages* const dbg) {
  canRetryGrouped = false;

  QImage visualized;
  if (dbg) {
    visualized = buildTrimBaseVisualization(contentBlocks, area);
  }

  // Don't trim too much.
  // There is a special case when there is nothing but garbage on the page: keep trimming tiny cuts.
  if (isTrimTooLarge(removedArea, newArea)) {
    addTrimDebugImage(dbg, visualized, newArea, removedArea, false, "trim_too_much");
    return area;
  }

  const double proximityBias = computeTrimProximityBias(content, text, garbage.image(), area, newArea, removedArea);

  BinaryImage remainingContent(contentBlocks.size(), WHITE);
  rasterOp<RopSrc>(remainingContent, newArea, content, newArea.topLeft());
  rasterOp<RopAnd<RopSrc, RopDst>>(remainingContent, newArea, contentBlocks, newArea.topLeft());

  const SEDM dmToOthers(remainingContent, SEDM::DIST_TO_BLACK, SEDM::DIST_TO_NO_BORDERS);
  remainingContent.release();

  double sumDistToGarbage = 0;
  double sumDistToOthers = 0;
  sumBorderDistances(contentBlocks, garbage.sedm(), dmToOthers, removedArea, sumDistToGarbage, sumDistToOthers);

  sumDistToGarbage *= proximityBias;
  sumDistToOthers *= 1.0 - proximityBias;

  if (sumDistToGarbage < sumDistToOthers) {
    garbage.add(content, removedArea);
    addTrimDebugImage(dbg, visualized, newArea, removedArea, true, "trimmed");
    return newArea;
  }

  addTrimDebugImage(dbg, visualized, newArea, removedArea, false, "not_trimmed");
  canRetryGrouped = (proximityBias < 0.85);
  return area;
}  // ContentBoxFinder::trim

void ContentBoxFinder::filterShadows(const TaskStatus& status,
                                     imageproc::BinaryImage& shadows,
                                     DebugImages* const dbg) {
  // The input image should only contain shadows from the edges
  // of a page, but in practice it may also contain things like
  // a black table header which white letters on it. Here we
  // try to filter them out.

  // Shadows that touch borders are genuine and should not be removed.
  BinaryImage borders(shadows.size(), WHITE);
  borders.fillExcept(borders.rect().adjusted(1, 1, -1, -1), BLACK);

  BinaryImage touchingShadows(seedFill(borders, shadows, CONN8));
  status.throwIfCancelled();
  rasterOp<RopXor<RopSrc, RopDst>>(shadows, touchingShadows);
  if (dbg) {
    dbg->add(shadows, "non_border_shadows");
  }

  if (shadows.countBlackPixels()) {
    BinaryImage invShadows(shadows.inverted());
    BinaryImage mask(seedFill(borders, invShadows, CONN8));
    status.throwIfCancelled();
    borders.release();
    rasterOp<RopOr<RopNot<RopDst>, RopSrc>>(mask, shadows);
    if (dbg) {
      dbg->add(mask, "shadows_no_holes");
    }

    BinaryImage textMask(
        estimateTextMask(status, invShadows, mask, DetectionOptions::DEFAULT_FILL_FACTOR / 100.0, dbg));
    invShadows.release();
    mask.release();
    textMask = seedFill(textMask, shadows, CONN8);
    status.throwIfCancelled();
    if (dbg) {
      dbg->add(textMask, "misclassified_shadows");
    }
    rasterOp<RopXor<RopSrc, RopDst>>(shadows, textMask);
  }

  rasterOp<RopOr<RopSrc, RopDst>>(shadows, touchingShadows);
}  // ContentBoxFinder::filterShadows

/*====================== ContentBoxFinder::Garbage =====================*/

ContentBoxFinder::Garbage::Garbage(const Type type, const BinaryImage& garbage)
    : m_garbage(garbage),
      m_sedmBorders(type == VERT ? SEDM::DIST_TO_VERT_BORDERS : SEDM::DIST_TO_HOR_BORDERS),
      m_sedmUpdatePending(true) {}

void ContentBoxFinder::Garbage::add(const BinaryImage& garbage, const QRect& rect) {
  rasterOp<RopOr<RopSrc, RopDst>>(m_garbage, rect, garbage, rect.topLeft());
  m_sedmUpdatePending = true;
}

const SEDM& ContentBoxFinder::Garbage::sedm() {
  if (m_sedmUpdatePending) {
    m_sedm = SEDM(m_garbage, SEDM::DIST_TO_BLACK, m_sedmBorders);
    m_sedmUpdatePending = false;
  }
  return m_sedm;
}
}  // namespace select_content
