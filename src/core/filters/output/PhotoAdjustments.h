// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_PHOTOADJUSTMENTS_H_
#define SCANTAILOR_OUTPUT_PHOTOADJUSTMENTS_H_

#include <cstdint>

class QDomDocument;
class QDomElement;
class QImage;
class QString;

namespace imageproc {
class BinaryImage;
}

namespace output {
/**
 * Manual photo adjustments of the color and grayscale output, like the basic panel of Lightroom.
 * Adopted from ScanTailor Spectre (weasel::PhotoAdjustments / TonalCurve), but applied to the
 * finished output instead of the source image, so that binarization, picture detection and OCR
 * are not affected.
 *
 * All sliders range from -100 to 100, the exposure from -2 to 2 stops. Every adjustment maps
 * each value to a new one regardless of the neighboring pixels (lookup tables).
 */
class PhotoAdjustments {
 public:
  static constexpr int MIN_VALUE = -100;
  static constexpr int MAX_VALUE = 100;
  static constexpr double MIN_EXPOSURE = -2.0;
  static constexpr double MAX_EXPOSURE = 2.0;

  PhotoAdjustments() = default;

  explicit PhotoAdjustments(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool operator==(const PhotoAdjustments& other) const;

  bool operator!=(const PhotoAdjustments& other) const;

  /** All sliders at zero and "Keep white" off: nothing to store. */
  bool isDefault() const;

  /** All sliders at zero: the image stays as it is. */
  bool isNeutral() const;

  /**
   * Applies the adjustments to an Indexed8 (grayscale), RGB32 or ARGB32 image in place.
   *
   * \param mask If not null, only pixels that are white in the mask are changed.
   * \param inverted The image holds inverted values (white text on black, processed inverted),
   *        so the adjustments are applied to the values as they will be shown.
   */
  void applyInPlace(QImage& image, const imageproc::BinaryImage* mask = nullptr, bool inverted = false) const;

  int temperature() const { return m_temperature; }
  void setTemperature(int value);

  int tint() const { return m_tint; }
  void setTint(int value);

  double exposure() const { return m_exposure; }
  void setExposure(double value);

  int contrast() const { return m_contrast; }
  void setContrast(int value);

  int highlights() const { return m_highlights; }
  void setHighlights(int value);

  int shadows() const { return m_shadows; }
  void setShadows(int value);

  int whites() const { return m_whites; }
  void setWhites(int value);

  int blacks() const { return m_blacks; }
  void setBlacks(int value);

  /** Pure white stays white (white background, white margins). */
  bool keepWhite() const { return m_keepWhite; }
  void setKeepWhite(bool keep) { m_keepWhite = keep; }

  /** The tone curve without temperature and tint, for tests. */
  void buildToneTable(uint8_t table[256]) const;

 private:
  int m_temperature = 0;
  int m_tint = 0;
  double m_exposure = 0.0;
  int m_contrast = 0;
  int m_highlights = 0;
  int m_shadows = 0;
  int m_whites = 0;
  int m_blacks = 0;
  bool m_keepWhite = false;
};
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_PHOTOADJUSTMENTS_H_
