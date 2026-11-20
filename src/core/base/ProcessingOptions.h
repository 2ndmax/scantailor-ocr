// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
#define SCANTAILOR_CORE_PROCESSINGOPTIONS_H_

#include <QStringList>

#include "ColorModeDetector.h"
#include "PdfCompression.h"
#include "TiffWriter.h"

class QDomDocument;
class QDomElement;
class QString;

/**
 * The settings of a project that apply to all its pages: how the output files are compressed,
 * how the PDF is made and the settings of the color mode and white on black detections.
 *
 * Each project stores them; new projects, and projects from before they were stored, take them
 * from the default parameters (DefaultParams). The stages get them from StageSequence, and each
 * task takes a copy when it is created, so a running batch keeps the values it was started with.
 */
struct ProcessingOptions {
  /** Which files the export creates. */
  struct Formats {
    bool pdf = true;
    bool txt = false;
    bool hocr = false;
    bool alto = false;

    bool any() const { return pdf || hasTextFormat(); }

    /** Whether a format made of nothing but the recognized text is chosen. */
    bool hasTextFormat() const { return txt || hocr || alto; }

    /** The one place that decides whether Tesseract has to run. */
    bool needsTextRecognition(const bool textLayer) const { return (pdf && textLayer) || hasTextFormat(); }

    bool operator==(const Formats& other) const {
      return (pdf == other.pdf) && (txt == other.txt) && (hocr == other.hocr) && (alto == other.alto);
    }

    bool operator!=(const Formats& other) const { return !(*this == other); }
  };

  /** How the PDF is made. */
  struct Pdf {
    /** Grayscale and colour pages and the pictures of split pages: NONE, DEFLATE, JPEG or JPEG2000. */
    PdfCompression colorCompression = PdfCompression::JPEG;
    /** 10 to 100. */
    int colorQuality = 85;
    /** The background of split pages is stored at 1 / backgroundScale of the output resolution: 1, 2 or 3. */
    int backgroundScale = 2;
    /** Posterized colour pages and pictures of split pages (see PdfImageEncoder::isPalette()): the same methods. */
    PdfCompression paletteCompression = PdfCompression::DEFLATE;
    /** 10 to 100. */
    int paletteQuality = 85;
    /** Black and white pages and the text of split pages: NONE, DEFLATE, CCITT_G4 or JBIG2. */
    PdfCompression bitonalCompression = PdfCompression::JBIG2;
    /** Write the PDF as PDF/A-2b. */
    bool pdfA = true;
    /** Add the recognized text as an invisible layer. Takes effect with text recognition. */
    bool textLayer = true;

    bool operator==(const Pdf& other) const {
      return (colorCompression == other.colorCompression) && (colorQuality == other.colorQuality)
             && (backgroundScale == other.backgroundScale) && (paletteCompression == other.paletteCompression)
             && (paletteQuality == other.paletteQuality) && (bitonalCompression == other.bitonalCompression)
             && (pdfA == other.pdfA) && (textLayer == other.textLayer);
    }

    bool operator!=(const Pdf& other) const { return !(*this == other); }
  };

  /** How the text is recognized. */
  struct Ocr {
    /** Language codes, e.g. "deu" and "eng". */
    QStringList languages{QStringLiteral("deu"), QStringLiteral("eng")};
    /** One of OcrEngine::PageLayout. */
    int pageLayout = 0;

    bool operator==(const Ocr& other) const {
      return (languages == other.languages) && (pageLayout == other.pageLayout);
    }

    bool operator!=(const Ocr& other) const { return !(*this == other); }
  };

  /** For the output files. */
  TiffWriter::Compression tiffCompression;
  /** For "Detect Color Mode". */
  ColorModeDetector::Options colorDetection;
  /** Whether the deskew step detects light content on a dark background. */
  bool blackOnWhiteDetection = true;
  /** Whether the output step takes over the detected background, unless it was set by hand. */
  bool blackOnWhiteAtOutput = true;
  Formats formats;
  Pdf pdf;
  Ocr ocr;

  ProcessingOptions() = default;

  /** Values missing in the element (or the whole element) are taken from \p fallback. */
  ProcessingOptions(const QDomElement& el, const ProcessingOptions& fallback);

  /** Whether any of the chosen files needs the text of the pages. */
  bool needsTextRecognition() const;

  QDomElement toXml(QDomDocument& doc, const QString& name) const;
};

#endif  // SCANTAILOR_CORE_PROCESSINGOPTIONS_H_
