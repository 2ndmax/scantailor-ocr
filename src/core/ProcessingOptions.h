// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
#define SCANTAILOR_CORE_PROCESSINGOPTIONS_H_

#include "ColorModeDetector.h"
#include "TiffWriter.h"
#include "filters/select_content/DetectionOptions.h"

/**
 * The settings outside the project that change the processing result.
 *
 * The processing doesn't read them from ApplicationSettings itself: the application hands them
 * to the stages (StageSequence::setProcessingOptions()), and each task takes a copy when it is
 * created. So a running batch keeps the values it was started with, and tests can set any values.
 * The defaults are those of ApplicationSettings.
 */
struct ProcessingOptions {
  /** For the output files. */
  TiffWriter::Compression tiffCompression;
  /** For the automatic content box. */
  select_content::DetectionOptions contentDetection;
  /** For "Detect Color Mode". */
  ColorModeDetector::Options colorDetection;
  /** Whether the deskew step detects black text on a white background. */
  bool blackOnWhiteDetection = true;
  /** Whether the output step takes over the detected black on white, unless it is set manually. */
  bool blackOnWhiteAtOutput = true;
};

#endif  // SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
