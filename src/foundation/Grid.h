// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_GRID_H_
#define SCANTAILOR_FOUNDATION_GRID_H_

#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

template <typename Node>
class Grid {
 public:
  /**
   * Creates a null grid.
   */
  Grid();

  /**
   * \brief Creates a width x height grid with specified padding on each side.
   */
  Grid(int width, int height, int padding);

  /**
   * \brief Creates a deep copy of another grid including padding.
   *
   * Stride is also preserved.
   */
  Grid(const Grid& other);

  Grid(Grid&& other) noexcept;

  Grid& operator=(const Grid& other);

  Grid& operator=(Grid&& other) noexcept;

  bool isNull() const { return m_width <= 0 || m_height <= 0; }

  void initPadding(const Node& paddingNode);

  void initInterior(const Node& interiorNode);

  /**
   * \brief Returns a pointer to the beginning of unpadded data.
   */
  Node* data() { return m_data; }

  /**
   * \brief Returns a pointer to the beginning of unpadded data.
   */
  const Node* data() const { return m_data; }

  /**
   * \brief Returns a pointer to the beginning of padded data.
   */
  Node* paddedData() { return m_storage.get(); }

  /**
   * \brief Returns a pointer to the beginning of padded data.
   */
  const Node* paddedData() const { return m_storage.get(); }

  /**
   * Returns the number of nodes in a row, including padding nodes.
   */
  int stride() const { return m_stride; }

  /**
   * Returns the number of nodes in a row, excluding padding nodes.
   */
  int width() const { return m_width; }

  /**
   * Returns the number of nodes in a column, excluding padding nodes.
   */
  int height() const { return m_height; }

  /**
   * Returns the number of padding layers from each side.
   */
  int padding() const { return m_padding; }

  void swap(Grid& other) noexcept;

 private:
  static size_t checkedStorageLen(int width, int height, int padding);
  static std::unique_ptr<Node[]> allocateStorage(int width, int height, int padding);

  template <typename T>
  static void basicSwap(T& o1, T& o2) noexcept {
    T tmp(std::move(o1));
    o1 = std::move(o2);
    o2 = std::move(tmp);
  }

  std::unique_ptr<Node[]> m_storage;
  Node* m_data;
  int m_width;
  int m_height;
  int m_stride;
  int m_padding;
};

template <typename Node>
size_t Grid<Node>::checkedStorageLen(const int width, const int height, const int padding) {
  if ((width < 0) || (height < 0) || (padding < 0)) {
    throw std::invalid_argument("Grid: negative dimension or padding");
  }
  const size_t pad2 = static_cast<size_t>(padding) * 2;
  const size_t stride = static_cast<size_t>(width) + pad2;
  const size_t totalHeight = static_cast<size_t>(height) + pad2;
  if ((stride > static_cast<size_t>((std::numeric_limits<int>::max)()))
      || (totalHeight > static_cast<size_t>((std::numeric_limits<int>::max)()))) {
    throw std::overflow_error("Grid: dimension overflows int");
  }
  if ((stride > 0) && (totalHeight > (std::numeric_limits<size_t>::max)() / stride)) {
    throw std::overflow_error("Grid: storage size overflow");
  }
  return stride * totalHeight;
}

template <typename Node>
std::unique_ptr<Node[]> Grid<Node>::allocateStorage(const int width, const int height, const int padding) {
  const size_t len = checkedStorageLen(width, height, padding);
  return len > 0 ? std::make_unique<Node[]>(len) : nullptr;
}

template <typename Node>
Grid<Node>::Grid() : m_data(nullptr), m_width(0), m_height(0), m_stride(0), m_padding(0) {}

template <typename Node>
Grid<Node>::Grid(int width, int height, int padding)
    : m_storage(allocateStorage(width, height, padding)),
      m_data(m_storage
                 ? (m_storage.get()
                    + (static_cast<size_t>(width) + static_cast<size_t>(padding) * 2) * static_cast<size_t>(padding)
                    + static_cast<size_t>(padding))
                 : nullptr),
      m_width(width),
      m_height(height),
      m_stride(static_cast<int>(static_cast<size_t>(width) + static_cast<size_t>(padding) * 2)),
      m_padding(padding) {}

template <typename Node>
Grid<Node>::Grid(const Grid& other)
    : m_storage(other.m_storage ? allocateStorage(other.m_width, other.m_height, other.m_padding) : nullptr),
      m_data(m_storage ? (m_storage.get() + static_cast<size_t>(other.m_stride) * static_cast<size_t>(other.m_padding)
                          + static_cast<size_t>(other.m_padding))
                       : nullptr),
      m_width(other.m_width),
      m_height(other.m_height),
      m_stride(other.m_stride),
      m_padding(other.m_padding) {
  if (m_storage) {
    const size_t len
        = static_cast<size_t>(m_stride) * (static_cast<size_t>(m_height) + static_cast<size_t>(m_padding) * 2);
    for (size_t i = 0; i < len; ++i) {
      m_storage[i] = other.m_storage[i];
    }
  }
}

template <typename Node>
Grid<Node>::Grid(Grid&& other) noexcept
    : m_storage(std::move(other.m_storage)),
      m_data(other.m_data),
      m_width(other.m_width),
      m_height(other.m_height),
      m_stride(other.m_stride),
      m_padding(other.m_padding) {
  other.m_data = nullptr;
  other.m_width = 0;
  other.m_height = 0;
  other.m_stride = 0;
  other.m_padding = 0;
}

template <typename Node>
Grid<Node>& Grid<Node>::operator=(const Grid& other) {
  if (this != &other) {
    Grid(other).swap(*this);
  }
  return *this;
}

template <typename Node>
Grid<Node>& Grid<Node>::operator=(Grid&& other) noexcept {
  if (this != &other) {
    m_storage = std::move(other.m_storage);
    m_data = other.m_data;
    m_width = other.m_width;
    m_height = other.m_height;
    m_stride = other.m_stride;
    m_padding = other.m_padding;
    other.m_data = nullptr;
    other.m_width = 0;
    other.m_height = 0;
    other.m_stride = 0;
    other.m_padding = 0;
  }
  return *this;
}

template <typename Node>
void Grid<Node>::initPadding(const Node& paddingNode) {
  if (!m_storage || (m_padding == 0)) {
    // Empty or no padding.
    return;
  }

  Node* line = m_storage.get();
  for (int row = 0; row < m_padding; ++row) {
    for (int x = 0; x < m_stride; ++x) {
      line[x] = paddingNode;
    }
    line += m_stride;
  }

  for (int y = 0; y < m_height; ++y) {
    for (int col = 0; col < m_padding; ++col) {
      line[col] = paddingNode;
    }
    for (int col = m_stride - m_padding; col < m_stride; ++col) {
      line[col] = paddingNode;
    }
    line += m_stride;
  }

  for (int row = 0; row < m_padding; ++row) {
    for (int x = 0; x < m_stride; ++x) {
      line[x] = paddingNode;
    }
    line += m_stride;
  }
}  // >::initPadding

template <typename Node>
void Grid<Node>::initInterior(const Node& interiorNode) {
  if (!m_storage) {
    return;
  }
  Node* line = m_data;
  for (int y = 0; y < m_height; ++y) {
    for (int x = 0; x < m_width; ++x) {
      line[x] = interiorNode;
    }
    line += m_stride;
  }
}

template <typename Node>
void Grid<Node>::swap(Grid& other) noexcept {
  m_storage.swap(other.m_storage);
  basicSwap(m_data, other.m_data);
  basicSwap(m_width, other.m_width);
  basicSwap(m_height, other.m_height);
  basicSwap(m_stride, other.m_stride);
  basicSwap(m_padding, other.m_padding);
}

template <typename Node>
void swap(Grid<Node>& o1, Grid<Node>& o2) noexcept {
  o1.swap(o2);
}

#endif  // ifndef SCANTAILOR_FOUNDATION_GRID_H_
