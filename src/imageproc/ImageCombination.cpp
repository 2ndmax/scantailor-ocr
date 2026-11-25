// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.


#include "ImageCombination.h"

#include <QImage>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "BinaryImage.h"

namespace imageproc {
namespace impl {
template <typename MixedPixel>
void combineImagesMono(QImage& mixedImage, const BinaryImage& foreground) {
  auto* mixedLine = reinterpret_cast<MixedPixel*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const uint32_t* foregroundLine = foreground.data();
  const int foregroundStride = foreground.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (foregroundLine[x >> 5] & (msb >> (x & 31))) {
        uint32_t tmp = foregroundLine[x >> 5];
        tmp >>= (31 - (x & 31));
        tmp &= uint32_t(1);

        --tmp;
        tmp |= 0xff000000;
        mixedLine[x] = static_cast<MixedPixel>(tmp);
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

template <typename MixedPixel>
void combineImagesMono(QImage& mixedImage, const BinaryImage& foreground, const BinaryImage& mask) {
  auto* mixedLine = reinterpret_cast<MixedPixel*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const uint32_t* foregroundLine = foreground.data();
  const int foregroundStride = foreground.wordsPerLine();
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        uint32_t tmp = foregroundLine[x >> 5];
        tmp >>= (31 - (x & 31));
        tmp &= uint32_t(1);

        --tmp;
        tmp |= 0xff000000;
        mixedLine[x] = static_cast<MixedPixel>(tmp);
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }
}

template <typename MixedPixel>
void combineImagesColor(QImage& mixedImage, const QImage& foreground) {
  auto* mixedLine = reinterpret_cast<MixedPixel*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const auto* foregroundLine = reinterpret_cast<const MixedPixel*>(foreground.bits());
  const int foregroundStride = foreground.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const auto msb = uint32_t(0x00ffffff);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if ((foregroundLine[x] & msb) != msb) {
        mixedLine[x] = foregroundLine[x];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

template <>
void combineImagesColor<uint8_t>(QImage& mixedImage, const QImage& foreground) {
  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const auto* foregroundLine = foreground.bits();
  const int foregroundStride = foreground.bytesPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();

  // Both images are grayscale (index i is gray i), so white is 0xff.
  // White foreground pixels are transparent, as in the 32-bit version.
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (foregroundLine[x] != 0xff) {
        mixedLine[x] = foregroundLine[x];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

template <typename MixedPixel, typename ForegroundPixel>
void combineImagesColor(QImage& mixedImage, const QImage& foreground);

template <>
void combineImagesColor<uint32_t, uint8_t>(QImage& mixedImage, const QImage& foreground) {
  auto* mixedLine = reinterpret_cast<uint32_t*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(uint32_t));
  const auto* foregroundLine = foreground.bits();
  const int foregroundStride = foreground.bytesPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const auto msb = uint32_t(0x00ffffff);

  const QVector<QRgb> foregroundPalette = foreground.colorTable();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t idx = foregroundLine[x];
      if (idx >= foregroundPalette.size()) {
        throw std::invalid_argument("ImageCombination: color index out of palette range.");
      }
      uint32_t color = foregroundPalette[idx];
      if ((color & msb) != msb) {
        mixedLine[x] = color;
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

bool isIdentityGray(const QImage& image) {
  if ((image.format() != QImage::Format_Indexed8) || !image.isGrayscale()) {
    return false;
  }
  const QVector<QRgb> palette = image.colorTable();
  if (palette.size() != 256) {
    return false;
  }
  for (int i = 0; i < 256; ++i) {
    if (palette[i] != qRgb(i, i, i)) {
      return false;
    }
  }
  return true;
}

void mergePalettes(QVector<uint32_t>& mixedPalette, const QVector<uint32_t>& palette) {
  std::unordered_set<uint32_t> mixedColors(mixedPalette.begin(), mixedPalette.end());
  for (uint32_t color : palette) {
    if (mixedColors.find(color) == mixedColors.end()) {
      mixedPalette.push_back(color);
      mixedColors.insert(color);
    }
  }
}

int nearestPaletteIndex(const QVector<uint32_t>& palette, const uint32_t color) {
  int nearest = 0;
  int nearestDistance = std::numeric_limits<int>::max();
  for (int i = 0; i < palette.size(); ++i) {
    const int dr = qRed(palette[i]) - qRed(color);
    const int dg = qGreen(palette[i]) - qGreen(color);
    const int db = qBlue(palette[i]) - qBlue(color);
    const int distance = dr * dr + dg * dg + db * db;
    if (distance < nearestDistance) {
      nearest = i;
      nearestDistance = distance;
    }
  }
  return nearest;
}

uint8_t resolveOrAddPaletteIndex(QVector<uint32_t>& palette,
                                 std::unordered_map<uint32_t, uint8_t>& colorToIndex,
                                 const uint32_t color) {
  const uint32_t opaque = color | 0xff000000u;
  const auto it = colorToIndex.find(opaque);
  if (it != colorToIndex.end()) {
    return it->second;
  }
  uint8_t idx = 0;
  if (palette.size() < 256) {
    palette.push_back(opaque);
    idx = static_cast<uint8_t>(palette.size() - 1);
  } else {
    idx = static_cast<uint8_t>(nearestPaletteIndex(palette, opaque));
  }
  colorToIndex[opaque] = idx;
  return idx;
}

std::unordered_map<uint32_t, uint8_t> buildColorToIndexMap(const QVector<uint32_t>& palette) {
  std::unordered_map<uint32_t, uint8_t> colorToIndex;
  for (int i = 0; i < palette.size() && i < 256; ++i) {
    colorToIndex[palette[i]] = static_cast<uint8_t>(i);
  }
  return colorToIndex;
}

// Adds the foreground colors to the palette of the 8-bit mixedImage and returns, for each foreground
// index, the index of its color in the mixed palette. The palette holds 256 colors at most, so a color
// that no longer fits gets the nearest color already in it.
std::vector<uint8_t> mergeForegroundPalette(QImage& mixedImage, const QVector<uint32_t>& foregroundPalette) {
  QVector<uint32_t> mixedPalette = mixedImage.colorTable();
  if (mixedPalette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  if (mixedPalette.size() < 256) {
    mergePalettes(mixedPalette, foregroundPalette);
    if (mixedPalette.size() > 256) {
      mixedPalette.resize(256);
    }
    mixedImage.setColorTable(mixedPalette);
  }

  const std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(mixedPalette);

  std::vector<uint8_t> toMixedIndex(256, 0);
  for (int i = 0; i < foregroundPalette.size() && i < 256; ++i) {
    const auto it = colorToIndex.find(foregroundPalette[i]);
    toMixedIndex[i] = (it != colorToIndex.end())
                          ? it->second
                          : static_cast<uint8_t>(nearestPaletteIndex(mixedPalette, foregroundPalette[i]));
  }
  return toMixedIndex;
}

template <>
void combineImagesColor<uint8_t, uint8_t>(QImage& mixedImage, const QImage& foreground) {
  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const auto* foregroundLine = foreground.bits();
  const int foregroundStride = foreground.bytesPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const auto msb = uint32_t(0x00ffffff);

  const QVector<uint32_t> foregroundPalette = foreground.colorTable();
  const std::vector<uint8_t> toMixedIndex = mergeForegroundPalette(mixedImage, foregroundPalette);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint8_t idx = foregroundLine[x];
      if (idx >= foregroundPalette.size()) {
        throw std::invalid_argument("ImageCombination: color index out of palette range.");
      }
      uint32_t color = foregroundPalette[idx];
      if ((color & msb) != msb) {
        mixedLine[x] = toMixedIndex[idx];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

template <>
void combineImagesColor<uint8_t, uint32_t>(QImage& mixedImage, const QImage& foreground) {
  QVector<uint32_t> mixedPalette = mixedImage.colorTable();
  if (mixedPalette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  const int initialPaletteSize = mixedPalette.size();
  std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(mixedPalette);

  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const auto* foregroundLine = reinterpret_cast<const uint32_t*>(foreground.bits());
  const int foregroundStride = foreground.bytesPerLine() / static_cast<int>(sizeof(uint32_t));
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const auto msb = uint32_t(0x00ffffff);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      const uint32_t color = foregroundLine[x];
      if ((color & msb) != msb) {
        mixedLine[x] = resolveOrAddPaletteIndex(mixedPalette, colorToIndex, color);
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }

  if (mixedPalette.size() != initialPaletteSize) {
    mixedImage.setColorTable(mixedPalette);
  }
}

template <typename MixedPixel>
void combineImagesColor(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  auto* mixedLine = reinterpret_cast<MixedPixel*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const auto* foregroundLine = reinterpret_cast<const MixedPixel*>(foreground.bits());
  const int foregroundStride = foreground.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        mixedLine[x] = foregroundLine[x];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }
}

template <typename MixedPixel, typename ForegroundPixel>
void combineImagesColor(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask);

template <>
void combineImagesColor<uint32_t, uint8_t>(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  auto* mixedLine = reinterpret_cast<uint32_t*>(mixedImage.bits());
  const int mixedStride = mixedImage.bytesPerLine() / static_cast<int>(sizeof(uint32_t));
  const auto* foregroundLine = foreground.bits();
  const int foregroundStride = foreground.bytesPerLine();
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  const QVector<QRgb> foregroundPalette = foreground.colorTable();

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        const uint8_t idx = foregroundLine[x];
        if (idx >= foregroundPalette.size()) {
          throw std::invalid_argument("ImageCombination: color index out of palette range.");
        }
        mixedLine[x] = foregroundPalette[idx];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }
}

template <>
void combineImagesColor<uint8_t, uint8_t>(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const auto* foregroundLine = foreground.bits();
  const int foregroundStride = foreground.bytesPerLine();
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  const QVector<uint32_t> foregroundPalette = foreground.colorTable();
  const std::vector<uint8_t> toMixedIndex = mergeForegroundPalette(mixedImage, foregroundPalette);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        const uint8_t idx = foregroundLine[x];
        if (idx >= foregroundPalette.size()) {
          throw std::invalid_argument("ImageCombination: color index out of palette range.");
        }
        mixedLine[x] = toMixedIndex[idx];
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }
}

template <>
void combineImagesColor<uint8_t, uint32_t>(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  QVector<uint32_t> mixedPalette = mixedImage.colorTable();
  if (mixedPalette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  const int initialPaletteSize = mixedPalette.size();
  std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(mixedPalette);

  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const auto* foregroundLine = reinterpret_cast<const uint32_t*>(foreground.bits());
  const int foregroundStride = foreground.bytesPerLine() / static_cast<int>(sizeof(uint32_t));
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        mixedLine[x] = resolveOrAddPaletteIndex(mixedPalette, colorToIndex, foregroundLine[x]);
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }

  if (mixedPalette.size() != initialPaletteSize) {
    mixedImage.setColorTable(mixedPalette);
  }
}

template <typename MixedPixel>
void applyMask(QImage& image, const BinaryImage& bwMask, const BWColor fillingColor = WHITE) {
  auto* imageLine = reinterpret_cast<MixedPixel*>(image.bits());
  const int imageStride = image.bytesPerLine() / static_cast<int>(sizeof(MixedPixel));
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;
  // Opaque black in 32 bits; in a gray palette image, both cast to the index of black or white.
  const auto fillingPixel = static_cast<MixedPixel>((fillingColor == WHITE) ? 0xffffffff : 0xff000000);

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        imageLine[x] = fillingPixel;
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}
}  // namespace impl

namespace {
inline void checkImageFormatSupported(const QImage& image) {
  if ((image.format() != QImage::Format_Indexed8) && (image.format() != QImage::Format_RGB32)
      && (image.format() != QImage::Format_ARGB32)) {
    throw std::invalid_argument("ImageCombination: wrong image format.");
  }
}

template <typename T1, typename T2>
inline void checkImagesHaveEqualSize(const T1& image1, const T2& image2) {
  if (image1.size() != image2.size()) {
    throw std::invalid_argument("ImageCombination: images size don't match.");
  }
}

template <typename T1, typename T2, typename T3>
inline void checkImagesHaveEqualSize(const T1& image1, const T2& image2, const T3& image3) {
  if ((image1.size() != image2.size()) || (image1.size() != image3.size())) {
    throw std::invalid_argument("ImageCombination: images size don't match.");
  }
}
}  // namespace

namespace impl {
void combineImagesMonoIndexed8(QImage& mixedImage, const BinaryImage& foreground) {
  QVector<uint32_t> mixedPalette = mixedImage.colorTable();
  if (mixedPalette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  const int initialPaletteSize = mixedPalette.size();
  std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(mixedPalette);
  const uint8_t blackIndex = resolveOrAddPaletteIndex(mixedPalette, colorToIndex, 0xff000000u);
  if (mixedPalette.size() != initialPaletteSize) {
    mixedImage.setColorTable(mixedPalette);
  }

  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const uint32_t* foregroundLine = foreground.data();
  const int foregroundStride = foreground.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (foregroundLine[x >> 5] & (msb >> (x & 31))) {
        mixedLine[x] = blackIndex;
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
  }
}

void combineImagesMonoIndexed8(QImage& mixedImage, const BinaryImage& foreground, const BinaryImage& mask) {
  QVector<uint32_t> mixedPalette = mixedImage.colorTable();
  if (mixedPalette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  const int initialPaletteSize = mixedPalette.size();
  std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(mixedPalette);
  const uint8_t blackIndex = resolveOrAddPaletteIndex(mixedPalette, colorToIndex, 0xff000000u);
  const uint8_t whiteIndex = resolveOrAddPaletteIndex(mixedPalette, colorToIndex, 0xffffffffu);
  if (mixedPalette.size() != initialPaletteSize) {
    mixedImage.setColorTable(mixedPalette);
  }

  auto* mixedLine = mixedImage.bits();
  const int mixedStride = mixedImage.bytesPerLine();
  const uint32_t* foregroundLine = foreground.data();
  const int foregroundStride = foreground.wordsPerLine();
  const uint32_t* maskLine = mask.data();
  const int maskStride = mask.wordsPerLine();
  const int width = mixedImage.width();
  const int height = mixedImage.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (maskLine[x >> 5] & (msb >> (x & 31))) {
        mixedLine[x] = (foregroundLine[x >> 5] & (msb >> (x & 31))) ? blackIndex : whiteIndex;
      }
    }
    mixedLine += mixedStride;
    foregroundLine += foregroundStride;
    maskLine += maskStride;
  }
}

void applyMaskIndexed8(QImage& image, const BinaryImage& bwMask, const BWColor fillingColor) {
  QVector<uint32_t> palette = image.colorTable();
  if (palette.isEmpty()) {
    throw std::invalid_argument("ImageCombination: empty color table.");
  }
  const int initialPaletteSize = palette.size();
  std::unordered_map<uint32_t, uint8_t> colorToIndex = buildColorToIndexMap(palette);
  const uint8_t fillIdx
      = resolveOrAddPaletteIndex(palette, colorToIndex, (fillingColor == WHITE) ? 0xffffffffu : 0xff000000u);
  if (palette.size() != initialPaletteSize) {
    image.setColorTable(palette);
  }

  auto* imageLine = image.bits();
  const int imageStride = image.bytesPerLine();
  const uint32_t* bwMaskLine = bwMask.data();
  const int bwMaskStride = bwMask.wordsPerLine();
  const int width = image.width();
  const int height = image.height();
  const uint32_t msb = uint32_t(1) << 31;

  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      if (!(bwMaskLine[x >> 5] & (msb >> (x & 31)))) {
        imageLine[x] = fillIdx;
      }
    }
    imageLine += imageStride;
    bwMaskLine += bwMaskStride;
  }
}

void combineImagesMono(QImage& mixedImage, const BinaryImage& foreground) {
  if (mixedImage.format() == QImage::Format_Indexed8) {
    if (isIdentityGray(mixedImage)) {
      combineImagesMono<uint8_t>(mixedImage, foreground);
    } else {
      combineImagesMonoIndexed8(mixedImage, foreground);
    }
  } else {
    combineImagesMono<uint32_t>(mixedImage, foreground);
  }
}

void combineImagesMono(QImage& mixedImage, const BinaryImage& foreground, const BinaryImage& mask) {
  if (mixedImage.format() == QImage::Format_Indexed8) {
    if (isIdentityGray(mixedImage)) {
      combineImagesMono<uint8_t>(mixedImage, foreground, mask);
    } else {
      combineImagesMonoIndexed8(mixedImage, foreground, mask);
    }
  } else {
    combineImagesMono<uint32_t>(mixedImage, foreground, mask);
  }
}

void combineImagesColor(QImage& mixedImage, const QImage& foreground) {
  if (mixedImage.format() == QImage::Format_Indexed8) {
    if ((foreground.format() == QImage::Format_RGB32) || (foreground.format() == QImage::Format_ARGB32)) {
      combineImagesColor<uint8_t, uint32_t>(mixedImage, foreground);
    } else if (isIdentityGray(mixedImage) && isIdentityGray(foreground)) {
      combineImagesColor<uint8_t>(mixedImage, foreground);
    } else {
      combineImagesColor<uint8_t, uint8_t>(mixedImage, foreground);
    }
  } else {
    if ((foreground.format() == QImage::Format_RGB32) || (foreground.format() == QImage::Format_ARGB32)) {
      combineImagesColor<uint32_t>(mixedImage, foreground);
    } else {
      combineImagesColor<uint32_t, uint8_t>(mixedImage, foreground);
    }
  }
}

void combineImagesColor(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  if (mixedImage.format() == QImage::Format_Indexed8) {
    if ((foreground.format() == QImage::Format_RGB32) || (foreground.format() == QImage::Format_ARGB32)) {
      combineImagesColor<uint8_t, uint32_t>(mixedImage, foreground, mask);
    } else if (isIdentityGray(mixedImage) && isIdentityGray(foreground)) {
      combineImagesColor<uint8_t>(mixedImage, foreground, mask);
    } else {
      combineImagesColor<uint8_t, uint8_t>(mixedImage, foreground, mask);
    }
  } else {
    if ((foreground.format() == QImage::Format_RGB32) || (foreground.format() == QImage::Format_ARGB32)) {
      combineImagesColor<uint32_t>(mixedImage, foreground, mask);
    } else {
      combineImagesColor<uint32_t, uint8_t>(mixedImage, foreground, mask);
    }
  }
}

void applyMask(QImage& image, const BinaryImage& bwMask, const BWColor fillingColor) {
  if (image.format() == QImage::Format_Indexed8) {
    if (isIdentityGray(image)) {
      applyMask<uint8_t>(image, bwMask, fillingColor);
    } else {
      applyMaskIndexed8(image, bwMask, fillingColor);
    }
  } else {
    applyMask<uint32_t>(image, bwMask, fillingColor);
  }
}
}  // namespace impl

void combineImages(QImage& mixedImage, const BinaryImage& foreground) {
  checkImageFormatSupported(mixedImage);
  checkImagesHaveEqualSize(mixedImage, foreground);

  impl::combineImagesMono(mixedImage, foreground);
}

void combineImages(QImage& mixedImage, const BinaryImage& foreground, const BinaryImage& mask) {
  checkImageFormatSupported(mixedImage);
  checkImagesHaveEqualSize(mixedImage, foreground, mask);

  impl::combineImagesMono(mixedImage, foreground, mask);
}

void combineImages(QImage& mixedImage, const QImage& foreground) {
  checkImageFormatSupported(mixedImage);
  checkImagesHaveEqualSize(mixedImage, foreground);

  if ((foreground.format() == QImage::Format_Mono) || (foreground.format() == QImage::Format_MonoLSB)) {
    impl::combineImagesMono(mixedImage, BinaryImage(foreground));
  } else {
    checkImageFormatSupported(foreground);
    impl::combineImagesColor(mixedImage, foreground);
  }
}

void combineImages(QImage& mixedImage, const QImage& foreground, const BinaryImage& mask) {
  checkImageFormatSupported(mixedImage);
  checkImagesHaveEqualSize(mixedImage, foreground, mask);

  if ((foreground.format() == QImage::Format_Mono) || (foreground.format() == QImage::Format_MonoLSB)) {
    impl::combineImagesMono(mixedImage, BinaryImage(foreground), mask);
  } else {
    checkImageFormatSupported(foreground);
    impl::combineImagesColor(mixedImage, foreground, mask);
  }
}

void applyMask(QImage& image, const BinaryImage& bwMask, const BWColor fillingColor) {
  checkImageFormatSupported(image);
  checkImagesHaveEqualSize(image, bwMask);

  impl::applyMask(image, bwMask, fillingColor);
}
}  // namespace imageproc
