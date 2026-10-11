// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_COLORMODEDETECTOR_H_
#define SCANTAILOR_CORE_COLORMODEDETECTOR_H_

#include <QString>

#include "LeptonicaDetector.h"

class QImage;
class TaskStatus;

/**
 * Detects whether a page should be output as black and white, grayscale, mixed or color.
 *
 * Starts from the detection of ScanTailor Spectre (LeptonicaDetector) and adds:
 * - Color only counts if enough of it lies outside picture areas, so colored headings, narrow
 *   boxes and thumb index tabs alone do not make a page a color page.
 * - Color that lies only in pictures makes the page Mixed.
 * - Optionally, our picture zone detection as a second opinion finds pictures that the Spectre
 *   detection misses (light or small photos).
 *
 * Only available if built with ENABLE_OCR (Leptonica comes with Tesseract).
 */
class ColorModeDetector {
 public:
  /** Stored with the results, so that pages are judged again when the detection changes. */
  static constexpr int VERSION = 1;

  enum class Mode { BlackWhite, Grayscale, Mixed, Color };

  struct Options {
    /** Share of the page that has to be colored outside picture areas for a color page. */
    double minColorShare = 0.03;
    /** Use our picture zone detection as a second opinion on black and white pages. */
    bool usePictureZones = false;

    bool operator==(const Options& other) const {
      return (minColorShare == other.minColorShare) && (usePictureZones == other.usePictureZones);
    }

    bool operator!=(const Options& other) const { return !(*this == other); }
  };

  struct Evidence {
    LeptonicaDetector::ColorType spectreVerdict = LeptonicaDetector::ColorType::BlackWhite;
    LeptonicaDetector::DetectionEvidence spectre;
    /** Whether the picture areas were measured; the values below are only set then. */
    bool picturesMeasured = false;
    double pictureShare = 0;
    double largestPicture = 0;
    double colorInPictures = 0;
    double colorOutside = 0;
    double textOutside = 0;
    QString reason;
  };

  /**
   * \param content The content area of the page (color or grayscale), with its resolution set.
   */
  static Mode detect(const QImage& content,
                     const Options& options,
                     const TaskStatus& status,
                     Evidence* evidence = nullptr);

  static const char* modeToString(Mode mode);
};

#endif  // SCANTAILOR_CORE_COLORMODEDETECTOR_H_
