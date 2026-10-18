// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PhotoAdjustments.h"

#include <BinaryImage.h>
#include <Grayscale.h>

#include <QDomDocument>
#include <QDomElement>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace imageproc;

namespace output {
namespace {
int clampValue(const int value) {
  return std::clamp(value, PhotoAdjustments::MIN_VALUE, PhotoAdjustments::MAX_VALUE);
}

double smoothstep(const double edge0, const double edge1, const double x) {
  const double t = std::clamp((x - edge0) / (edge1 - edge0), 0.0, 1.0);
  return t * t * (3.0 - 2.0 * t);
}

uint8_t toByte(const double value) {
  return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0), 0L, 255L));
}

bool isWhiteInMask(const uint32_t* maskLine, const int x) {
  const uint32_t msb = uint32_t(1) << 31;
  return !(maskLine[x >> 5] & (msb >> (x & 31)));
}
}  // namespace

PhotoAdjustments::PhotoAdjustments(const QDomElement& el)
    : m_temperature(clampValue(el.attribute("temperature").toInt())),
      m_tint(clampValue(el.attribute("tint").toInt())),
      m_exposure(std::clamp(el.attribute("exposure").toDouble(), MIN_EXPOSURE, MAX_EXPOSURE)),
      m_contrast(clampValue(el.attribute("contrast").toInt())),
      m_highlights(clampValue(el.attribute("highlights").toInt())),
      m_shadows(clampValue(el.attribute("shadows").toInt())),
      m_whites(clampValue(el.attribute("whites").toInt())),
      m_blacks(clampValue(el.attribute("blacks").toInt())),
      m_keepWhite(el.attribute("keepWhite") == "1") {}

QDomElement PhotoAdjustments::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("temperature", m_temperature);
  el.setAttribute("tint", m_tint);
  el.setAttribute("exposure", m_exposure);
  el.setAttribute("contrast", m_contrast);
  el.setAttribute("highlights", m_highlights);
  el.setAttribute("shadows", m_shadows);
  el.setAttribute("whites", m_whites);
  el.setAttribute("blacks", m_blacks);
  el.setAttribute("keepWhite", m_keepWhite ? "1" : "0");
  return el;
}

bool PhotoAdjustments::operator==(const PhotoAdjustments& other) const {
  return m_temperature == other.m_temperature && m_tint == other.m_tint && m_exposure == other.m_exposure
         && m_contrast == other.m_contrast && m_highlights == other.m_highlights && m_shadows == other.m_shadows
         && m_whites == other.m_whites && m_blacks == other.m_blacks && m_keepWhite == other.m_keepWhite;
}

bool PhotoAdjustments::operator!=(const PhotoAdjustments& other) const {
  return !(*this == other);
}

bool PhotoAdjustments::isDefault() const {
  return isNeutral() && !m_keepWhite;
}

bool PhotoAdjustments::isNeutral() const {
  return m_temperature == 0 && m_tint == 0 && m_exposure == 0.0 && m_contrast == 0 && m_highlights == 0
         && m_shadows == 0 && m_whites == 0 && m_blacks == 0;
}

void PhotoAdjustments::setTemperature(const int value) {
  m_temperature = clampValue(value);
}

void PhotoAdjustments::setTint(const int value) {
  m_tint = clampValue(value);
}

void PhotoAdjustments::setExposure(const double value) {
  m_exposure = std::clamp(std::round(value * 100.0) / 100.0, MIN_EXPOSURE, MAX_EXPOSURE);
}

void PhotoAdjustments::setContrast(const int value) {
  m_contrast = clampValue(value);
}

void PhotoAdjustments::setHighlights(const int value) {
  m_highlights = clampValue(value);
}

void PhotoAdjustments::setShadows(const int value) {
  m_shadows = clampValue(value);
}

void PhotoAdjustments::setWhites(const int value) {
  m_whites = clampValue(value);
}

void PhotoAdjustments::setBlacks(const int value) {
  m_blacks = clampValue(value);
}

void PhotoAdjustments::buildToneTable(uint8_t table[256]) const {
  // The formulas of ScanTailor Spectre, applied in the order of the sliders:
  // exposure sets the overall brightness, contrast shapes the midtones, highlights and shadows
  // the bright and dark parts, whites and blacks the ends of the range.
  for (int i = 0; i < 256; ++i) {
    double val = i / 255.0;

    // Exposure: in stops.
    if (m_exposure != 0.0) {
      val *= std::pow(2.0, m_exposure);
    }
    // Contrast: around the middle gray.
    if (m_contrast != 0) {
      const double factor = std::pow(2.0, m_contrast / 100.0 * 1.3);
      val = 0.5 + (val - 0.5) * factor;
    }
    // Highlights: the bright part (positive = brighter).
    if (m_highlights != 0) {
      val += m_highlights / 100.0 * 0.40 * smoothstep(0.25, 1.0, val);
    }
    // Shadows: the dark part (positive = brighter).
    if (m_shadows != 0) {
      val += m_shadows / 100.0 * 0.40 * (1.0 - smoothstep(0.0, 0.75, val));
    }
    // Whites: the white point (positive = brighter whites).
    if (m_whites != 0) {
      const double wp = m_whites / 100.0 * 0.25;
      if (wp > 0.0) {
        val += (1.0 - val) * wp * smoothstep(0.5, 1.0, val);
      } else {
        val += val * wp * smoothstep(0.5, 1.0, val);
      }
    }
    // Blacks: the black point (positive = lighter blacks).
    if (m_blacks != 0) {
      const double bp = m_blacks / 100.0 * 0.25;
      if (bp > 0.0) {
        val = bp + val * (1.0 - bp);
      } else {
        val = std::max(0.0, val + bp) / (1.0 + bp);
      }
    }

    table[i] = toByte(val);
  }
}

void PhotoAdjustments::applyInPlace(QImage& image, const BinaryImage* mask, const bool inverted) const {
  if (isNeutral() || image.isNull()) {
    return;
  }
  if (mask && (mask->width() != image.width() || mask->height() != image.height())) {
    throw std::invalid_argument("PhotoAdjustments: the mask doesn't match the image.");
  }

  uint8_t tone[256];
  buildToneTable(tone);

  // Temperature: positive = warmer (more red, less blue). Tint: positive = magenta (less green).
  const double redFactor = 1.0 + m_temperature / 100.0 * 0.30;
  const double blueFactor = 1.0 - m_temperature / 100.0 * 0.30;
  const double greenFactor = 1.0 - m_tint / 100.0 * 0.30;

  // Tables for the values as stored in the image (inverted, if the image is processed inverted).
  uint8_t gray[256], red[256], green[256], blue[256];
  for (int i = 0; i < 256; ++i) {
    const int shown = inverted ? 255 - i : i;
    const double base = tone[shown] / 255.0;
    uint8_t g = tone[shown];
    uint8_t r = toByte(base * redFactor);
    uint8_t gr = toByte(base * greenFactor);
    uint8_t b = toByte(base * blueFactor);
    if (inverted) {
      g = 255 - g;
      r = 255 - r;
      gr = 255 - gr;
      b = 255 - b;
    }
    gray[i] = g;
    red[i] = r;
    green[i] = gr;
    blue[i] = b;
  }
  const int white = inverted ? 0 : 255;

  const int width = image.width();
  const int height = image.height();
  const uint32_t* maskLine = mask ? mask->data() : nullptr;
  const int maskStride = mask ? mask->wordsPerLine() : 0;

  switch (image.format()) {
    case QImage::Format_Indexed8: {
      // Grayscale output has the identity gray palette; anything else is turned into it first.
      const QVector<QRgb> palette = image.colorTable();
      bool identity = palette.size() == 256;
      for (int i = 0; identity && i < 256; ++i) {
        identity = palette[i] == qRgb(i, i, i);
      }
      uint8_t toGray[256];
      for (int i = 0; i < 256; ++i) {
        toGray[i] = i < palette.size() ? static_cast<uint8_t>(qGray(palette[i])) : 0;
      }
      if (!identity) {
        image.setColorTable(createGrayscalePalette());
      }

      for (int y = 0; y < height; ++y) {
        auto* line = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
          const uint8_t value = identity ? line[x] : toGray[line[x]];
          const bool change = (!maskLine || isWhiteInMask(maskLine, x)) && !(m_keepWhite && value == white);
          line[x] = change ? gray[value] : value;
        }
        maskLine += maskStride;
      }
      break;
    }
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32: {
      const QRgb whitePixel = inverted ? 0x000000u : 0xFFFFFFu;
      for (int y = 0; y < height; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < width; ++x) {
          const QRgb px = line[x];
          if ((maskLine && !isWhiteInMask(maskLine, x)) || (m_keepWhite && (px & 0xFFFFFFu) == whitePixel)) {
            continue;
          }
          line[x] = qRgba(red[qRed(px)], green[qGreen(px)], blue[qBlue(px)], qAlpha(px));
        }
        maskLine += maskStride;
      }
      break;
    }
    default:
      throw std::invalid_argument("PhotoAdjustments: wrong image format.");
  }
}
}  // namespace output
