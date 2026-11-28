// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "InfluenceMap.h"

#include <QImage>
#include <cassert>
#include <cstddef>
#include <stdexcept>

#include "BinaryImage.h"
#include "BitOps.h"
#include "ConnectivityMap.h"

namespace imageproc {
namespace {
struct NeighborOffset {
  int dx;
  int dy;
  int16_t vxDelta;
  int16_t vyDelta;
  int32_t distDelta;
};

constexpr NeighborOffset kNeighborOffsets[8] = {
    {-1, -1, 1, 1, 2},  // North-western
    {0, -1, 0, 1, 1},   // Northern
    {1, -1, -1, 1, 2},  // North-eastern
    {1, 0, -1, 0, 1},   // Eastern
    {1, 1, -1, -1, 2},  // South-eastern
    {0, 1, 0, -1, 1},   // Southern
    {-1, 1, 1, -1, 2},  // South-western
    {-1, 0, 1, 0, 1},   // Western
};

void seedQueueFromCmap(InfluenceMap::Cell* cell,
                       const uint32_t* label,
                       const size_t totalCells,
                       [[maybe_unused]] const uint32_t maxLabel,
                       FastQueue<InfluenceMap::Cell*>& queue) {
  for (size_t i = totalCells; i > 0; --i) {
    assert(*label <= maxLabel);
    cell->label = *label;
    cell->distSq = 0;
    cell->vec.x = 0;
    cell->vec.y = 0;
    if (*label != 0) {
      queue.push(cell);
    }
    ++cell;
    ++label;
  }
}

void markMaskedBackground(InfluenceMap::Cell* cell, const int width, const int height, const BinaryImage& mask) {
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const uint32_t msb = uint32_t(1) << 31;
  for (int y = 0; y < height - 2; ++y) {
    for (int x = 0; x < width - 2; ++x, ++cell) {
      if ((maskLine[x >> 5] & (msb >> (x & 31))) && (cell->label == 0)) {
        cell->distSq = ~uint32_t(0);
      }
    }
    maskLine += maskStride;
    cell += 2;
  }
}

void markUnmaskedBackground(InfluenceMap::Cell* cell, const int width, const int height) {
  for (int y = 0; y < height - 2; ++y) {
    for (int x = 0; x < width - 2; ++x, ++cell) {
      if (cell->label == 0) {
        cell->distSq = ~uint32_t(0);
      }
    }
    cell += 2;
  }
}

void propagateInfluence([[maybe_unused]] const InfluenceMap::Cell* baseData,
                        const int width,
                        [[maybe_unused]] const int height,
                        [[maybe_unused]] const uint32_t maxLabel,
                        FastQueue<InfluenceMap::Cell*>& queue) {
  while (!queue.empty()) {
    InfluenceMap::Cell* const cell = queue.front();
    queue.pop();

    assert((cell - baseData) / width > 0);
    assert((cell - baseData) / width < height - 1);
    assert((cell - baseData) % width > 0);
    assert((cell - baseData) % width < width - 1);
    assert(cell->distSq != ~uint32_t(0));
    assert(cell->label != 0);
    assert(cell->label <= maxLabel);

    const int32_t dx2 = cell->vec.x * 2;
    const int32_t dy2 = cell->vec.y * 2;

    for (const auto& offset : kNeighborOffsets) {
      InfluenceMap::Cell* const nbh = cell + offset.dy * width + offset.dx;
      const uint32_t newDistSq
          = cell->distSq + static_cast<uint32_t>(dx2 * offset.vxDelta + dy2 * offset.vyDelta + offset.distDelta);
      if (newDistSq < nbh->distSq) {
        nbh->label = cell->label;
        nbh->distSq = newDistSq;
        nbh->vec.x = static_cast<int16_t>(cell->vec.x + offset.vxDelta);
        nbh->vec.y = static_cast<int16_t>(cell->vec.y + offset.vyDelta);
        queue.push(nbh);
      }
    }
  }
}
}  // namespace

InfluenceMap::InfluenceMap() : m_plainData(nullptr), m_size(), m_stride(0), m_maxLabel(0) {}

InfluenceMap::InfluenceMap(const ConnectivityMap& cmap) : m_plainData(nullptr), m_size(), m_stride(0), m_maxLabel(0) {
  if (cmap.size().isEmpty()) {
    return;
  }

  init(cmap);
}

InfluenceMap::InfluenceMap(const ConnectivityMap& cmap, const BinaryImage& mask)
    : m_plainData(nullptr), m_size(), m_stride(0), m_maxLabel(0) {
  if (cmap.size().isEmpty()) {
    return;
  }
  if (cmap.size() != mask.size()) {
    throw std::invalid_argument("InfluenceMap: cmap and mask have different sizes");
  }

  init(cmap, &mask);
}

InfluenceMap::InfluenceMap(const InfluenceMap& other)
    : m_data(other.m_data),
      m_plainData(nullptr),
      m_size(other.size()),
      m_stride(other.stride()),
      m_maxLabel(other.m_maxLabel) {
  if (!m_size.isEmpty()) {
    m_plainData = &m_data[0] + m_stride + 1;
  }
}

InfluenceMap& InfluenceMap::operator=(const InfluenceMap& other) {
  InfluenceMap(other).swap(*this);
  return *this;
}

void InfluenceMap::swap(InfluenceMap& other) {
  m_data.swap(other.m_data);
  std::swap(m_plainData, other.m_plainData);
  std::swap(m_size, other.m_size);
  std::swap(m_stride, other.m_stride);
  std::swap(m_maxLabel, other.m_maxLabel);
}

void InfluenceMap::init(const ConnectivityMap& cmap, const BinaryImage* mask) {
  if ((cmap.size().width() > 32767) || (cmap.size().height() > 32767)) {
    throw std::invalid_argument("InfluenceMap: image dimensions exceed 32767");
  }

  const int width = cmap.size().width() + 2;
  const int height = cmap.size().height() + 2;

  m_size = cmap.size();
  m_stride = width;
  const size_t totalCells = static_cast<size_t>(width) * static_cast<size_t>(height);
  m_data.resize(totalCells);
  m_plainData = &m_data[0] + width + 1;
  m_maxLabel = cmap.maxLabel();

  FastQueue<Cell*> queue;
  seedQueueFromCmap(&m_data[0], cmap.paddedData(), totalCells, m_maxLabel, queue);

  if (mask) {
    markMaskedBackground(m_plainData, width, height, *mask);
  } else {
    markUnmaskedBackground(m_plainData, width, height);
  }

  propagateInfluence(&m_data[0], width, height, m_maxLabel, queue);
}  // InfluenceMap::init

QImage InfluenceMap::visualized() const {
  if (m_size.isEmpty()) {
    return QImage();
  }

  const int width = m_size.width();
  const int height = m_size.height();

  QImage dst(m_size, QImage::Format_ARGB32);
  dst.fill(0x00FFFFFF);  // transparent white
  const Cell* srcLine = m_plainData;
  const int srcStride = m_stride;

  auto* dstLine = reinterpret_cast<uint32_t*>(dst.bits());
  const int dstStride = dst.bytesPerLine() / static_cast<int>(sizeof(uint32_t));

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t val = srcLine[x].label;
      if (val == 0) {
        continue;
      }

      const int bitsUnused = countMostSignificantZeroes(val);
      const uint32_t reversed = reverseBits(val) >> bitsUnused;
      const uint32_t mask = ~uint32_t(0) >> bitsUnused;

      const double H = 0.99 * (double(reversed) / mask);
      const double S = 1.0;
      const double V = 1.0;
      QColor color;
      color.setHsvF(H, S, V, 1.0);

      dstLine[x] = color.rgba();
    }
    srcLine += srcStride;
    dstLine += dstStride;
  }
  return dst;
}  // InfluenceMap::visualized
}  // namespace imageproc
