// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProcessingOptions.h"

#include <QDomDocument>
#include <QDomElement>
#include <QtGlobal>
#include <algorithm>
#include <cmath>
#include <iterator>

namespace {
int intAttribute(const QDomElement& el, const QString& name, const int fallback, const int min, const int max) {
  bool ok = false;
  const int value = el.attribute(name).toInt(&ok);
  return ok ? qBound(min, value, max) : fallback;
}

// A code that the TIFF writer does not know, or that does not fit the kind of image, would make
// every page fail to write, so such a value counts as damaged. The lists are the methods offered by
// TiffCompressionPanel.
template <size_t N>
int compressionAttribute(const QDomElement& el, const QString& name, const int fallback, const int (&allowed)[N]) {
  const int value = intAttribute(el, name, fallback, 0, 65535);
  return std::find(std::begin(allowed), std::end(allowed), value) != std::end(allowed) ? value : fallback;
}

constexpr int BW_COMPRESSIONS[]
    = {COMPRESSION_NONE, COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA, COMPRESSION_CCITTFAX4};
constexpr int COLOR_COMPRESSIONS[]
    = {COMPRESSION_NONE, COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA, COMPRESSION_JPEG};
constexpr int PALETTE_COMPRESSIONS[] = {COMPRESSION_NONE, COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA};

bool boolAttribute(const QDomElement& el, const QString& name, const bool fallback) {
  const QString value = el.attribute(name);
  return value.isEmpty() ? fallback : (value != "0");
}

PdfCompression pdfCompressionAttribute(const QDomElement& el, const QString& name, const PdfCompression fallback) {
  return static_cast<PdfCompression>(intAttribute(el, name, static_cast<int>(fallback),
                                                  static_cast<int>(PdfCompression::NONE),
                                                  static_cast<int>(PdfCompression::JBIG2)));
}

// The shares of the color mode detection are stored in percent, as they are shown.
int toPercent(const double share) {
  return static_cast<int>(std::lround(share * 100));
}
}  // namespace

ProcessingOptions::ProcessingOptions(const QDomElement& el, const ProcessingOptions& fallback) {
  const QDomElement tiffEl = el.namedItem("tiff").toElement();
  const TiffWriter::Compression& tiff = fallback.tiffCompression;
  tiffCompression.bw = compressionAttribute(tiffEl, "bw", tiff.bw, BW_COMPRESSIONS);
  tiffCompression.color = compressionAttribute(tiffEl, "color", tiff.color, COLOR_COMPRESSIONS);
  tiffCompression.jpegQuality
      = intAttribute(tiffEl, "jpegQuality", tiff.jpegQuality, TiffWriter::Compression::MIN_JPEG_QUALITY,
                     TiffWriter::Compression::MAX_JPEG_QUALITY);
  tiffCompression.palette = compressionAttribute(tiffEl, "palette", tiff.palette, PALETTE_COMPRESSIONS);

  const QDomElement colorEl = el.namedItem("color-detection").toElement();
  const ColorModeDetector::Options& color = fallback.colorDetection;
  colorDetection.minColorShare = intAttribute(colorEl, "minColor", toPercent(color.minColorShare), 0, 100) / 100.0;
  colorDetection.usePictureZones = boolAttribute(colorEl, "pictureZones", color.usePictureZones);
  colorDetection.minPictureShare = intAttribute(colorEl, "minPicture", toPercent(color.minPictureShare), 1, 95) / 100.0;

  const QDomElement blackOnWhiteEl = el.namedItem("black-on-white").toElement();
  blackOnWhiteDetection = boolAttribute(blackOnWhiteEl, "detection", fallback.blackOnWhiteDetection);
  blackOnWhiteAtOutput = boolAttribute(blackOnWhiteEl, "output", fallback.blackOnWhiteAtOutput);

  const QDomElement pdfEl = el.namedItem("pdf").toElement();
  const Pdf& pdfFallback = fallback.pdf;
  pdf.colorCompression = pdfCompressionAttribute(pdfEl, "colorCompression", pdfFallback.colorCompression);
  pdf.colorQuality = intAttribute(pdfEl, "colorQuality", pdfFallback.colorQuality, 10, 100);
  pdf.backgroundScale = intAttribute(pdfEl, "backgroundScale", pdfFallback.backgroundScale, 1, 3);
  pdf.paletteCompression = pdfCompressionAttribute(pdfEl, "paletteCompression", pdfFallback.paletteCompression);
  pdf.paletteQuality = intAttribute(pdfEl, "paletteQuality", pdfFallback.paletteQuality, 10, 100);
  pdf.bitonalCompression = pdfCompressionAttribute(pdfEl, "bitonalCompression", pdfFallback.bitonalCompression);
  pdf.pdfA = boolAttribute(pdfEl, "pdfA", pdfFallback.pdfA);

  const QDomElement ocrEl = el.namedItem("ocr").toElement();
  pdf.ocr = boolAttribute(ocrEl, "enabled", pdfFallback.ocr);
  // Stored the way Tesseract expects them, e.g. "deu+eng".
  pdf.ocrLanguages = ocrEl.hasAttribute("languages") ? ocrEl.attribute("languages").split('+', Qt::SkipEmptyParts)
                                                     : pdfFallback.ocrLanguages;
  pdf.ocrPageLayout = intAttribute(ocrEl, "pageLayout", pdfFallback.ocrPageLayout, 0, 2);
}

QDomElement ProcessingOptions::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el = doc.createElement(name);

  QDomElement tiffEl = doc.createElement("tiff");
  tiffEl.setAttribute("bw", tiffCompression.bw);
  tiffEl.setAttribute("color", tiffCompression.color);
  tiffEl.setAttribute("jpegQuality", tiffCompression.jpegQuality);
  tiffEl.setAttribute("palette", tiffCompression.palette);
  el.appendChild(tiffEl);

  QDomElement colorEl = doc.createElement("color-detection");
  colorEl.setAttribute("minColor", toPercent(colorDetection.minColorShare));
  colorEl.setAttribute("pictureZones", colorDetection.usePictureZones ? "1" : "0");
  colorEl.setAttribute("minPicture", toPercent(colorDetection.minPictureShare));
  el.appendChild(colorEl);

  QDomElement blackOnWhiteEl = doc.createElement("black-on-white");
  blackOnWhiteEl.setAttribute("detection", blackOnWhiteDetection ? "1" : "0");
  blackOnWhiteEl.setAttribute("output", blackOnWhiteAtOutput ? "1" : "0");
  el.appendChild(blackOnWhiteEl);

  QDomElement pdfEl = doc.createElement("pdf");
  pdfEl.setAttribute("colorCompression", static_cast<int>(pdf.colorCompression));
  pdfEl.setAttribute("colorQuality", pdf.colorQuality);
  pdfEl.setAttribute("backgroundScale", pdf.backgroundScale);
  pdfEl.setAttribute("paletteCompression", static_cast<int>(pdf.paletteCompression));
  pdfEl.setAttribute("paletteQuality", pdf.paletteQuality);
  pdfEl.setAttribute("bitonalCompression", static_cast<int>(pdf.bitonalCompression));
  pdfEl.setAttribute("pdfA", pdf.pdfA ? "1" : "0");
  el.appendChild(pdfEl);

  QDomElement ocrEl = doc.createElement("ocr");
  ocrEl.setAttribute("enabled", pdf.ocr ? "1" : "0");
  ocrEl.setAttribute("languages", pdf.ocrLanguages.join('+'));
  ocrEl.setAttribute("pageLayout", pdf.ocrPageLayout);
  el.appendChild(ocrEl);
  return el;
}
