// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_DETECTIONOPTIONS_H_
#define SCANTAILOR_SELECT_CONTENT_DETECTIONOPTIONS_H_

#include <ApplicationSettings.h>

namespace select_content {
/**
 * Settings of the automatic content box detection (adopted from ScanTailor Spectre).
 * The defaults reproduce the detection as it was before these settings existed. They are
 * program settings and are not stored in the project.
 */
class DetectionOptions {
 public:
  static constexpr int DEFAULT_FILL_FACTOR = 65;
  static constexpr int MIN_FILL_FACTOR = 30;
  static constexpr int MAX_FILL_FACTOR = 100;
  static constexpr int DEFAULT_BORDER_TOLERANCE = 0;
  static constexpr int MAX_BORDER_TOLERANCE = 100;

  DetectionOptions() = default;

  DetectionOptions(int fillFactor, int borderTolerance)
      : m_fillFactor(fillFactor), m_borderTolerance(borderTolerance) {}

  // The current program settings.
  static DetectionOptions fromSettings() {
    const ApplicationSettings& settings = ApplicationSettings::getInstance();
    return DetectionOptions(settings.getContentFillFactor(), settings.getContentBorderTolerance());
  }

  /**
   * In percent. The highest share of black pixels a text line may have. Higher values keep
   * pictures and dark areas as content; from 86 % on, shadows are no longer subtracted, and
   * from 95 % on, the whole page box is taken as content.
   */
  int fillFactor() const { return m_fillFactor; }

  void setFillFactor(int fillFactor) { m_fillFactor = fillFactor; }

  /**
   * In percent. How much of the content touching the image border is kept. 0 removes it as far
   * as before (up to a quarter of the shorter image side), 100 removes only the border itself.
   */
  int borderTolerance() const { return m_borderTolerance; }

  void setBorderTolerance(int borderTolerance) { m_borderTolerance = borderTolerance; }

  bool operator==(const DetectionOptions& other) const {
    return (m_fillFactor == other.m_fillFactor) && (m_borderTolerance == other.m_borderTolerance);
  }

  bool operator!=(const DetectionOptions& other) const { return !(*this == other); }

 private:
  int m_fillFactor = DEFAULT_FILL_FACTOR;
  int m_borderTolerance = DEFAULT_BORDER_TOLERANCE;
};
}  // namespace select_content
#endif  // ifndef SCANTAILOR_SELECT_CONTENT_DETECTIONOPTIONS_H_
