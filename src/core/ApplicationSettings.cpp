// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplicationSettings.h"

#include <tiff.h>

#include <QLocale>
#include <QtCore/QSettings>

#include "PdfCompression.h"

const bool ApplicationSettings::DEFAULT_OPENGL_STATE = false;
const QString ApplicationSettings::DEFAULT_COLOR_SCHEME = "dark";
const bool ApplicationSettings::DEFAULT_AUTO_SAVE_PROJECT = false;
const int ApplicationSettings::DEFAULT_TIFF_BW_COMPRESSION = COMPRESSION_CCITTFAX4;
const int ApplicationSettings::DEFAULT_TIFF_COLOR_COMPRESSION = COMPRESSION_LZW;
const int ApplicationSettings::DEFAULT_TIFF_PALETTE_COMPRESSION = COMPRESSION_LZW;
const int ApplicationSettings::DEFAULT_TIFF_JPEG_QUALITY = 85;
const bool ApplicationSettings::DEFAULT_BLACK_ON_WHITE_DETECTION = true;
const bool ApplicationSettings::DEFAULT_BLACK_ON_WHITE_DETECTION_OUTPUT = true;
const bool ApplicationSettings::DEFAULT_HIGHLIGHT_DEVIATION = true;
const double ApplicationSettings::DEFAULT_DESKEW_DEVIATION_COEF = 1.5;
const double ApplicationSettings::DEFAULT_DESKEW_DEVIATION_THRESHOLD = 1.0;
const double ApplicationSettings::DEFAULT_SELECT_CONTENT_DEVIATION_COEF = 0.35;
const double ApplicationSettings::DEFAULT_SELECT_CONTENT_DEVIATION_THRESHOLD = 1.0;
const double ApplicationSettings::DEFAULT_MARGINS_DEVIATION_COEF = 0.35;
const double ApplicationSettings::DEFAULT_MARGINS_DEVIATION_THRESHOLD = 1.0;
const QSize ApplicationSettings::DEFAULT_THUMBNAIL_QUALITY = QSize(200, 200);
const QSizeF ApplicationSettings::DEFAULT_MAX_LOGICAL_THUMBNAIL_SIZE = QSizeF(250, 160);
const bool ApplicationSettings::DEFAULT_SINGLE_COLUMN_THUMBNAIL_DISPLAY = false;
const QString ApplicationSettings::DEFAULT_LANGUAGE = QLocale::system().name();
const QString ApplicationSettings::DEFAULT_UNITS = "mm";
const QString ApplicationSettings::DEFAULT_PROFILE = "Default";
const bool ApplicationSettings::DEFAULT_SHOW_CANCELING_SELECTION_QUESTION = true;

const QString ApplicationSettings::ROOT_KEY = "settings";
const QString ApplicationSettings::OPENGL_STATE_KEY = "enable_opengl";
const QString ApplicationSettings::AUTO_SAVE_PROJECT_KEY = "auto_save_project";
const QString ApplicationSettings::COLOR_SCHEME_KEY = "color_scheme";
const QString ApplicationSettings::TIFF_BW_COMPRESSION_KEY = "bw_compression";
const QString ApplicationSettings::TIFF_COLOR_COMPRESSION_KEY = "color_compression";
const QString ApplicationSettings::TIFF_PALETTE_COMPRESSION_KEY = "tiff_palette_compression";
const QString ApplicationSettings::TIFF_JPEG_QUALITY_KEY = "tiff_jpeg_quality";
const QString ApplicationSettings::BLACK_ON_WHITE_DETECTION_KEY = "black_on_white_detection";
const QString ApplicationSettings::BLACK_ON_WHITE_DETECTION_OUTPUT_KEY = "black_on_white_detection_at_output";
const QString ApplicationSettings::HIGHLIGHT_DEVIATION_KEY = "highlight_deviation";
const QString ApplicationSettings::DESKEW_DEVIATION_COEF_KEY = "deskew_deviation_coef";
const QString ApplicationSettings::DESKEW_DEVIATION_THRESHOLD_KEY = "deskew_deviation_threshold";
const QString ApplicationSettings::SELECT_CONTENT_DEVIATION_COEF_KEY = "select_content_deviation_coef";
const QString ApplicationSettings::SELECT_CONTENT_DEVIATION_THRESHOLD_KEY = "select_content_deviation_threshold";
const QString ApplicationSettings::MARGINS_DEVIATION_COEF_KEY = "margins_deviation_coef";
const QString ApplicationSettings::MARGINS_DEVIATION_THRESHOLD_KEY = "margins_deviation_threshold";
const QString ApplicationSettings::THUMBNAIL_QUALITY_KEY = "thumbnail_quality";
const QString ApplicationSettings::MAX_LOGICAL_THUMBNAIL_SIZE_KEY = "max_logical_thumb_size";
const QString ApplicationSettings::SINGLE_COLUMN_THUMBNAIL_DISPLAY_KEY = "single_column_thumbnail_display";
const QString ApplicationSettings::LANGUAGE_KEY = "language";
const QString ApplicationSettings::UNITS_KEY = "units";
const QString ApplicationSettings::CURRENT_PROFILE_KEY = "current_profile";
const QString ApplicationSettings::SHOW_CANCELING_SELECTION_QUESTION_KEY = "selection_canceling_question";
const QString ApplicationSettings::DEFAULT_ZONE_CREATION_MODE_KEY = "default_zone_creation_mode";
const QString ApplicationSettings::OUTPUT_SHOW_GUIDES_KEY = "output_show_guides";
const int ApplicationSettings::DEFAULT_ZONE_CREATION_MODE = 0;  // POLYGONAL
const bool ApplicationSettings::DEFAULT_OUTPUT_SHOW_GUIDES = false;
const QString ApplicationSettings::DESKEW_HANDLE_DISTANCE_KEY = "deskew_handle_distance";
const int ApplicationSettings::DEFAULT_DESKEW_HANDLE_DISTANCE = 90;
const int ApplicationSettings::MIN_DESKEW_HANDLE_DISTANCE = 30;
const int ApplicationSettings::MAX_DESKEW_HANDLE_DISTANCE = 100;
const QString ApplicationSettings::PDF_JPEG_QUALITY_KEY = "pdf_jpeg_quality";
const QString ApplicationSettings::PDF_BACKGROUND_SCALE_KEY = "pdf_background_scale";
const QString ApplicationSettings::PDF_OPEN_AFTER_CREATION_KEY = "pdf_open_after_creation";
const QString ApplicationSettings::PDF_JBIG2_KEY = "pdf_jbig2";
const QString ApplicationSettings::PDF_COLOR_COMPRESSION_KEY = "pdf_color_compression";
const QString ApplicationSettings::PDF_PALETTE_COMPRESSION_KEY = "pdf_palette_compression";
const QString ApplicationSettings::PDF_PALETTE_QUALITY_KEY = "pdf_palette_quality";
const QString ApplicationSettings::PDF_BITONAL_COMPRESSION_KEY = "pdf_bitonal_compression";
const int ApplicationSettings::DEFAULT_PDF_JPEG_QUALITY = 85;
const int ApplicationSettings::DEFAULT_PDF_BACKGROUND_SCALE = 2;
const bool ApplicationSettings::DEFAULT_PDF_OPEN_AFTER_CREATION = true;
const bool ApplicationSettings::DEFAULT_PDF_JBIG2 = true;
const QString ApplicationSettings::PDF_OCR_ENABLED_KEY = "pdf_ocr_enabled";
const QString ApplicationSettings::PDF_OCR_LANGUAGES_KEY = "pdf_ocr_languages";
const QString ApplicationSettings::PDF_OCR_PAGE_LAYOUT_KEY = "pdf_ocr_page_layout";

QString ApplicationSettings::getKey(const QString& keyName) {
  return ApplicationSettings::ROOT_KEY + '/' + keyName;
}

ApplicationSettings::ApplicationSettings() = default;

ApplicationSettings& ApplicationSettings::getInstance() {
  static ApplicationSettings instance;
  return instance;
}

QVariant ApplicationSettings::readValue(const QString& key, const QVariant& defaultValue) const {
  const QMutexLocker locker(&m_mutex);
  return m_settings.value(key, defaultValue);
}

void ApplicationSettings::writeValue(const QString& key, const QVariant& value) {
  const QMutexLocker locker(&m_mutex);
  m_settings.setValue(key, value);
}

bool ApplicationSettings::isOpenGlEnabled() const {
  return readValue(getKey(OPENGL_STATE_KEY), DEFAULT_OPENGL_STATE).toBool();
}

void ApplicationSettings::setOpenGlEnabled(bool enabled) {
  writeValue(getKey(OPENGL_STATE_KEY), enabled);
}

QString ApplicationSettings::getColorScheme() const {
  return readValue(getKey(COLOR_SCHEME_KEY), DEFAULT_COLOR_SCHEME).toString();
}

void ApplicationSettings::setColorScheme(const QString& scheme) {
  writeValue(getKey(COLOR_SCHEME_KEY), scheme);
}

bool ApplicationSettings::isAutoSaveProjectEnabled() const {
  return readValue(getKey(AUTO_SAVE_PROJECT_KEY), DEFAULT_AUTO_SAVE_PROJECT).toBool();
}

void ApplicationSettings::setAutoSaveProjectEnabled(bool enabled) {
  writeValue(getKey(AUTO_SAVE_PROJECT_KEY), enabled);
}

int ApplicationSettings::getTiffBwCompression() const {
  return readValue(getKey(TIFF_BW_COMPRESSION_KEY), DEFAULT_TIFF_BW_COMPRESSION).toInt();
}

void ApplicationSettings::setTiffBwCompression(int compression) {
  writeValue(getKey(TIFF_BW_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getTiffColorCompression() const {
  return readValue(getKey(TIFF_COLOR_COMPRESSION_KEY), DEFAULT_TIFF_COLOR_COMPRESSION).toInt();
}

void ApplicationSettings::setTiffColorCompression(int compression) {
  writeValue(getKey(TIFF_COLOR_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getTiffPaletteCompression() const {
  return readValue(getKey(TIFF_PALETTE_COMPRESSION_KEY), DEFAULT_TIFF_PALETTE_COMPRESSION).toInt();
}

void ApplicationSettings::setTiffPaletteCompression(int compression) {
  writeValue(getKey(TIFF_PALETTE_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getTiffJpegQuality() const {
  bool ok = false;
  const int v = readValue(getKey(TIFF_JPEG_QUALITY_KEY), DEFAULT_TIFF_JPEG_QUALITY).toInt(&ok);
  return ok ? qBound(10, v, 100) : DEFAULT_TIFF_JPEG_QUALITY;
}

void ApplicationSettings::setTiffJpegQuality(const int quality) {
  writeValue(getKey(TIFF_JPEG_QUALITY_KEY), qBound(10, quality, 100));
}

bool ApplicationSettings::isBlackOnWhiteDetectionEnabled() const {
  return readValue(getKey(BLACK_ON_WHITE_DETECTION_KEY), DEFAULT_BLACK_ON_WHITE_DETECTION).toBool();
}

void ApplicationSettings::setBlackOnWhiteDetectionEnabled(bool enabled) {
  writeValue(getKey(BLACK_ON_WHITE_DETECTION_KEY), enabled);
}

bool ApplicationSettings::isBlackOnWhiteDetectionOutputEnabled() const {
  return readValue(getKey(BLACK_ON_WHITE_DETECTION_OUTPUT_KEY), DEFAULT_BLACK_ON_WHITE_DETECTION_OUTPUT).toBool();
}

void ApplicationSettings::setBlackOnWhiteDetectionOutputEnabled(bool enabled) {
  writeValue(getKey(BLACK_ON_WHITE_DETECTION_OUTPUT_KEY), enabled);
}

bool ApplicationSettings::isHighlightDeviationEnabled() const {
  return readValue(getKey(HIGHLIGHT_DEVIATION_KEY), DEFAULT_HIGHLIGHT_DEVIATION).toBool();
}

void ApplicationSettings::setHighlightDeviationEnabled(bool enabled) {
  writeValue(getKey(HIGHLIGHT_DEVIATION_KEY), enabled);
}

double ApplicationSettings::getDeskewDeviationCoef() const {
  return readValue(getKey(DESKEW_DEVIATION_COEF_KEY), DEFAULT_DESKEW_DEVIATION_COEF).toDouble();
}

void ApplicationSettings::setDeskewDeviationCoef(double value) {
  writeValue(getKey(DESKEW_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getDeskewDeviationThreshold() const {
  return readValue(getKey(DESKEW_DEVIATION_THRESHOLD_KEY), DEFAULT_DESKEW_DEVIATION_THRESHOLD).toDouble();
}

void ApplicationSettings::setDeskewDeviationThreshold(double value) {
  writeValue(getKey(DESKEW_DEVIATION_THRESHOLD_KEY), value);
}

double ApplicationSettings::getSelectContentDeviationCoef() const {
  return readValue(getKey(SELECT_CONTENT_DEVIATION_COEF_KEY), DEFAULT_SELECT_CONTENT_DEVIATION_COEF).toDouble();
}

void ApplicationSettings::setSelectContentDeviationCoef(double value) {
  writeValue(getKey(SELECT_CONTENT_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getSelectContentDeviationThreshold() const {
  return readValue(getKey(SELECT_CONTENT_DEVIATION_THRESHOLD_KEY), DEFAULT_SELECT_CONTENT_DEVIATION_THRESHOLD)
      .toDouble();
}

void ApplicationSettings::setSelectContentDeviationThreshold(double value) {
  writeValue(getKey(SELECT_CONTENT_DEVIATION_THRESHOLD_KEY), value);
}

double ApplicationSettings::getMarginsDeviationCoef() const {
  return readValue(getKey(MARGINS_DEVIATION_COEF_KEY), DEFAULT_MARGINS_DEVIATION_COEF).toDouble();
}

void ApplicationSettings::setMarginsDeviationCoef(double value) {
  writeValue(getKey(MARGINS_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getMarginsDeviationThreshold() const {
  return readValue(getKey(MARGINS_DEVIATION_THRESHOLD_KEY), DEFAULT_MARGINS_DEVIATION_THRESHOLD).toDouble();
}

void ApplicationSettings::setMarginsDeviationThreshold(double value) {
  writeValue(getKey(MARGINS_DEVIATION_THRESHOLD_KEY), value);
}

QSize ApplicationSettings::getThumbnailQuality() const {
  return readValue(getKey(THUMBNAIL_QUALITY_KEY), DEFAULT_THUMBNAIL_QUALITY).toSize();
}

void ApplicationSettings::setThumbnailQuality(const QSize& quality) {
  writeValue(getKey(THUMBNAIL_QUALITY_KEY), quality);
}

QSizeF ApplicationSettings::getMaxLogicalThumbnailSize() const {
  return readValue(getKey(MAX_LOGICAL_THUMBNAIL_SIZE_KEY), DEFAULT_MAX_LOGICAL_THUMBNAIL_SIZE).toSizeF();
}

void ApplicationSettings::setMaxLogicalThumbnailSize(const QSizeF& size) {
  writeValue(getKey(MAX_LOGICAL_THUMBNAIL_SIZE_KEY), size);
}

bool ApplicationSettings::isSingleColumnThumbnailDisplayEnabled() const {
  return readValue(getKey(SINGLE_COLUMN_THUMBNAIL_DISPLAY_KEY), DEFAULT_SINGLE_COLUMN_THUMBNAIL_DISPLAY).toBool();
}

void ApplicationSettings::setSingleColumnThumbnailDisplayEnabled(bool enabled) {
  writeValue(getKey(SINGLE_COLUMN_THUMBNAIL_DISPLAY_KEY), enabled);
}

QString ApplicationSettings::getLanguage() const {
  return readValue(getKey(LANGUAGE_KEY), DEFAULT_LANGUAGE).toString();
}

void ApplicationSettings::setLanguage(const QString& language) {
  writeValue(getKey(LANGUAGE_KEY), language);
}

QString ApplicationSettings::getUnits() const {
  return readValue(getKey(UNITS_KEY), DEFAULT_UNITS).toString();
}

void ApplicationSettings::setUnits(const QString& units) {
  writeValue(getKey(UNITS_KEY), units);
}

QString ApplicationSettings::getCurrentProfile() const {
  return readValue(getKey(CURRENT_PROFILE_KEY), DEFAULT_PROFILE).toString();
}

void ApplicationSettings::setCurrentProfile(const QString& profile) {
  writeValue(getKey(CURRENT_PROFILE_KEY), profile);
}

bool ApplicationSettings::isCancelingSelectionQuestionEnabled() {
  return readValue(getKey(SHOW_CANCELING_SELECTION_QUESTION_KEY), DEFAULT_SHOW_CANCELING_SELECTION_QUESTION).toBool();
}

void ApplicationSettings::setCancelingSelectionQuestionEnabled(bool enabled) {
  writeValue(getKey(SHOW_CANCELING_SELECTION_QUESTION_KEY), enabled);
}

int ApplicationSettings::getDefaultZoneCreationMode() const {
  const int v = readValue(getKey(DEFAULT_ZONE_CREATION_MODE_KEY), DEFAULT_ZONE_CREATION_MODE).toInt();
  return (v >= 0 && v <= 2) ? v : DEFAULT_ZONE_CREATION_MODE;
}

void ApplicationSettings::setDefaultZoneCreationMode(const int mode) {
  if (mode >= 0 && mode <= 2) {
    writeValue(getKey(DEFAULT_ZONE_CREATION_MODE_KEY), mode);
  }
}

bool ApplicationSettings::isOutputShowGuidesEnabled() const {
  return readValue(getKey(OUTPUT_SHOW_GUIDES_KEY), DEFAULT_OUTPUT_SHOW_GUIDES).toBool();
}

void ApplicationSettings::setOutputShowGuidesEnabled(bool enabled) {
  writeValue(getKey(OUTPUT_SHOW_GUIDES_KEY), enabled);
}

int ApplicationSettings::getDeskewHandleDistance() const {
  bool ok = false;
  const int v = readValue(getKey(DESKEW_HANDLE_DISTANCE_KEY), DEFAULT_DESKEW_HANDLE_DISTANCE).toInt(&ok);
  if (!ok) {
    return DEFAULT_DESKEW_HANDLE_DISTANCE;
  }
  return qBound(MIN_DESKEW_HANDLE_DISTANCE, v, MAX_DESKEW_HANDLE_DISTANCE);
}

void ApplicationSettings::setDeskewHandleDistance(const int percent) {
  writeValue(getKey(DESKEW_HANDLE_DISTANCE_KEY),
             qBound(MIN_DESKEW_HANDLE_DISTANCE, percent, MAX_DESKEW_HANDLE_DISTANCE));
}

int ApplicationSettings::getPdfColorCompression() const {
  bool ok = false;
  const int v = readValue(getKey(PDF_COLOR_COMPRESSION_KEY), static_cast<int>(PdfCompression::JPEG)).toInt(&ok);
  return ok ? v : static_cast<int>(PdfCompression::JPEG);
}

void ApplicationSettings::setPdfColorCompression(const int compression) {
  writeValue(getKey(PDF_COLOR_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getPdfColorQuality() const {
  // The key dates from when JPEG was the only choice.
  bool ok = false;
  const int v = readValue(getKey(PDF_JPEG_QUALITY_KEY), DEFAULT_PDF_JPEG_QUALITY).toInt(&ok);
  return ok ? qBound(10, v, 100) : DEFAULT_PDF_JPEG_QUALITY;
}

void ApplicationSettings::setPdfColorQuality(const int quality) {
  writeValue(getKey(PDF_JPEG_QUALITY_KEY), qBound(10, quality, 100));
}

int ApplicationSettings::getPdfPaletteCompression() const {
  bool ok = false;
  const int v = readValue(getKey(PDF_PALETTE_COMPRESSION_KEY), static_cast<int>(PdfCompression::DEFLATE)).toInt(&ok);
  return ok ? v : static_cast<int>(PdfCompression::DEFLATE);
}

void ApplicationSettings::setPdfPaletteCompression(const int compression) {
  writeValue(getKey(PDF_PALETTE_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getPdfPaletteQuality() const {
  bool ok = false;
  const int v = readValue(getKey(PDF_PALETTE_QUALITY_KEY), DEFAULT_PDF_JPEG_QUALITY).toInt(&ok);
  return ok ? qBound(10, v, 100) : DEFAULT_PDF_JPEG_QUALITY;
}

void ApplicationSettings::setPdfPaletteQuality(const int quality) {
  writeValue(getKey(PDF_PALETTE_QUALITY_KEY), qBound(10, quality, 100));
}

int ApplicationSettings::getPdfBitonalCompression() const {
  // Earlier versions only had the choice between JBIG2 and CCITT G4.
  const bool jbig2 = readValue(getKey(PDF_JBIG2_KEY), DEFAULT_PDF_JBIG2).toBool();
  const int fallback = static_cast<int>(jbig2 ? PdfCompression::JBIG2 : PdfCompression::CCITT_G4);
  bool ok = false;
  const int v = readValue(getKey(PDF_BITONAL_COMPRESSION_KEY), fallback).toInt(&ok);
  return ok ? v : fallback;
}

void ApplicationSettings::setPdfBitonalCompression(const int compression) {
  writeValue(getKey(PDF_BITONAL_COMPRESSION_KEY), compression);
}

int ApplicationSettings::getPdfBackgroundScale() const {
  bool ok = false;
  const int v = readValue(getKey(PDF_BACKGROUND_SCALE_KEY), DEFAULT_PDF_BACKGROUND_SCALE).toInt(&ok);
  return ok ? qBound(1, v, 3) : DEFAULT_PDF_BACKGROUND_SCALE;
}

void ApplicationSettings::setPdfBackgroundScale(const int scale) {
  writeValue(getKey(PDF_BACKGROUND_SCALE_KEY), qBound(1, scale, 3));
}

bool ApplicationSettings::isPdfOpenAfterCreationEnabled() const {
  return readValue(getKey(PDF_OPEN_AFTER_CREATION_KEY), DEFAULT_PDF_OPEN_AFTER_CREATION).toBool();
}

void ApplicationSettings::setPdfOpenAfterCreationEnabled(const bool enabled) {
  writeValue(getKey(PDF_OPEN_AFTER_CREATION_KEY), enabled);
}

bool ApplicationSettings::isPdfOcrEnabled() const {
  return readValue(getKey(PDF_OCR_ENABLED_KEY), true).toBool();
}

void ApplicationSettings::setPdfOcrEnabled(const bool enabled) {
  writeValue(getKey(PDF_OCR_ENABLED_KEY), enabled);
}

QStringList ApplicationSettings::getPdfOcrLanguages() const {
  // Stored the way Tesseract expects them, e.g. "deu+eng".
  const QString value = readValue(getKey(PDF_OCR_LANGUAGES_KEY), QStringLiteral("deu+eng")).toString();
  return value.split('+', Qt::SkipEmptyParts);
}

void ApplicationSettings::setPdfOcrLanguages(const QStringList& languages) {
  writeValue(getKey(PDF_OCR_LANGUAGES_KEY), languages.join('+'));
}

int ApplicationSettings::getPdfOcrPageLayout() const {
  bool ok = false;
  const int v = readValue(getKey(PDF_OCR_PAGE_LAYOUT_KEY), 0).toInt(&ok);
  return ok ? qBound(0, v, 2) : 0;
}

void ApplicationSettings::setPdfOcrPageLayout(const int layout) {
  writeValue(getKey(PDF_OCR_PAGE_LAYOUT_KEY), qBound(0, layout, 2));
}
