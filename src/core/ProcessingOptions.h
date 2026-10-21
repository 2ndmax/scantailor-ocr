// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
#define SCANTAILOR_CORE_PROCESSINGOPTIONS_H_

#include <QStringList>

#include "ColorModeDetector.h"
#include "PdfCompression.h"
#include "TiffWriter.h"
#include "filters/select_content/DetectionOptions.h"

class QDomDocument;
class QDomElement;
class QString;

/**
 * The settings of a project that apply to all its pages: how the output files are compressed,
 * how the PDF is made and the settings of the automatic detections.
 *
 * Each project stores them; new projects, and projects from before they were stored, take them
 * from the default parameters (DefaultParams). The stages get them from StageSequence, and each
 * task takes a copy when it is created, so a running batch keeps the values it was started with.
 */
struct ProcessingOptions {
  /** How the PDF is made, see PdfExportOptions. */
  struct Pdf {
    PdfCompression colorCompression = PdfCompression::JPEG;
    /** 10 to 100. */
    int colorQuality = 85;
    /** 1, 2 or 3. */
    int backgroundScale = 2;
    PdfCompression paletteCompression = PdfCompression::DEFLATE;
    /** 10 to 100. */
    int paletteQuality = 85;
    PdfCompression bitonalCompression = PdfCompression::JBIG2;
    /** Add an invisible text layer. */
    bool ocr = true;
    /** Language codes, e.g. "deu" and "eng". */
    QStringList ocrLanguages{QStringLiteral("deu"), QStringLiteral("eng")};
    /** One of OcrEngine::PageLayout. */
    int ocrPageLayout = 0;
  };

  /** For the output files. */
  TiffWriter::Compression tiffCompression;
  /** For the automatic content box. */
  select_content::DetectionOptions contentDetection;
  /** For "Detect Color Mode". */
  ColorModeDetector::Options colorDetection;
  /** Whether the deskew step detects light content on a dark background. */
  bool blackOnWhiteDetection = true;
  /** Whether the output step takes over the detected background, unless it was set by hand. */
  bool blackOnWhiteAtOutput = true;
  Pdf pdf;

  ProcessingOptions() = default;

  /** Values missing in the element (or the whole element) are taken from \p fallback. */
  ProcessingOptions(const QDomElement& el, const ProcessingOptions& fallback);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;
};

#endif  // SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
