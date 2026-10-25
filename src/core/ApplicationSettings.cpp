// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ApplicationSettings.h"

#include <QLocale>
#include <QtCore/QSettings>
#include <cmath>

#include "ThreadBudget.h"

const bool ApplicationSettings::DEFAULT_OPENGL_STATE = false;
const QString ApplicationSettings::DEFAULT_COLOR_SCHEME = "dark";
const bool ApplicationSettings::DEFAULT_AUTO_SAVE_PROJECT = false;
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
const QString ApplicationSettings::ACCENT_COLOR_KEY = "accent_color";
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
const QString ApplicationSettings::PAGE_LAYOUT_SHOW_MIDDLE_RECT_KEY = "page_layout_show_middle_rect";
const bool ApplicationSettings::DEFAULT_PAGE_LAYOUT_SHOW_MIDDLE_RECT = true;
const QString ApplicationSettings::DESKEW_HANDLE_DISTANCE_KEY = "deskew_handle_distance";
const int ApplicationSettings::DEFAULT_DESKEW_HANDLE_DISTANCE = 90;
const int ApplicationSettings::MIN_DESKEW_HANDLE_DISTANCE = 30;
const int ApplicationSettings::MAX_DESKEW_HANDLE_DISTANCE = 100;
// The key of the former slider in the batch processing panel, so that its value is kept.
const QString ApplicationSettings::PROCESSING_THREADS_KEY = "batch_processing_threads";
const QString ApplicationSettings::PDF_OPEN_AFTER_CREATION_KEY = "pdf_open_after_creation";
const bool ApplicationSettings::DEFAULT_PDF_OPEN_AFTER_CREATION = true;

QString ApplicationSettings::getKey(const QString& keyName) {
  return ApplicationSettings::ROOT_KEY + '/' + keyName;
}

ApplicationSettings::ApplicationSettings() = default;

ApplicationSettings& ApplicationSettings::getInstance() {
  static ApplicationSettings instance;
  return instance;
}

// Settings files can be damaged or edited by hand. These return the stored value only if it can be used.
static double positiveOrDefault(const double value, const double defaultValue) {
  return (std::isfinite(value) && value > 0.0) ? value : defaultValue;
}

static double nonNegativeOrDefault(const double value, const double defaultValue) {
  return (std::isfinite(value) && value >= 0.0) ? value : defaultValue;
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

QColor ApplicationSettings::getAccentColor() const {
  const QString name = readValue(getKey(ACCENT_COLOR_KEY), QString()).toString();
  return name.isEmpty() ? QColor() : QColor(name);
}

void ApplicationSettings::setAccentColor(const QColor& color) {
  writeValue(getKey(ACCENT_COLOR_KEY), color.isValid() ? color.name() : QString());
}

bool ApplicationSettings::isAutoSaveProjectEnabled() const {
  return readValue(getKey(AUTO_SAVE_PROJECT_KEY), DEFAULT_AUTO_SAVE_PROJECT).toBool();
}

void ApplicationSettings::setAutoSaveProjectEnabled(bool enabled) {
  writeValue(getKey(AUTO_SAVE_PROJECT_KEY), enabled);
}

bool ApplicationSettings::isHighlightDeviationEnabled() const {
  return readValue(getKey(HIGHLIGHT_DEVIATION_KEY), DEFAULT_HIGHLIGHT_DEVIATION).toBool();
}

void ApplicationSettings::setHighlightDeviationEnabled(bool enabled) {
  writeValue(getKey(HIGHLIGHT_DEVIATION_KEY), enabled);
}

double ApplicationSettings::getDeskewDeviationCoef() const {
  return positiveOrDefault(readValue(getKey(DESKEW_DEVIATION_COEF_KEY), DEFAULT_DESKEW_DEVIATION_COEF).toDouble(),
                           DEFAULT_DESKEW_DEVIATION_COEF);
}

void ApplicationSettings::setDeskewDeviationCoef(double value) {
  writeValue(getKey(DESKEW_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getDeskewDeviationThreshold() const {
  return nonNegativeOrDefault(
      readValue(getKey(DESKEW_DEVIATION_THRESHOLD_KEY), DEFAULT_DESKEW_DEVIATION_THRESHOLD).toDouble(),
      DEFAULT_DESKEW_DEVIATION_THRESHOLD);
}

void ApplicationSettings::setDeskewDeviationThreshold(double value) {
  writeValue(getKey(DESKEW_DEVIATION_THRESHOLD_KEY), value);
}

double ApplicationSettings::getSelectContentDeviationCoef() const {
  return positiveOrDefault(
      readValue(getKey(SELECT_CONTENT_DEVIATION_COEF_KEY), DEFAULT_SELECT_CONTENT_DEVIATION_COEF).toDouble(),
      DEFAULT_SELECT_CONTENT_DEVIATION_COEF);
}

void ApplicationSettings::setSelectContentDeviationCoef(double value) {
  writeValue(getKey(SELECT_CONTENT_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getSelectContentDeviationThreshold() const {
  return nonNegativeOrDefault(
      readValue(getKey(SELECT_CONTENT_DEVIATION_THRESHOLD_KEY), DEFAULT_SELECT_CONTENT_DEVIATION_THRESHOLD).toDouble(),
      DEFAULT_SELECT_CONTENT_DEVIATION_THRESHOLD);
}

void ApplicationSettings::setSelectContentDeviationThreshold(double value) {
  writeValue(getKey(SELECT_CONTENT_DEVIATION_THRESHOLD_KEY), value);
}

double ApplicationSettings::getMarginsDeviationCoef() const {
  return positiveOrDefault(readValue(getKey(MARGINS_DEVIATION_COEF_KEY), DEFAULT_MARGINS_DEVIATION_COEF).toDouble(),
                           DEFAULT_MARGINS_DEVIATION_COEF);
}

void ApplicationSettings::setMarginsDeviationCoef(double value) {
  writeValue(getKey(MARGINS_DEVIATION_COEF_KEY), value);
}

double ApplicationSettings::getMarginsDeviationThreshold() const {
  return nonNegativeOrDefault(
      readValue(getKey(MARGINS_DEVIATION_THRESHOLD_KEY), DEFAULT_MARGINS_DEVIATION_THRESHOLD).toDouble(),
      DEFAULT_MARGINS_DEVIATION_THRESHOLD);
}

void ApplicationSettings::setMarginsDeviationThreshold(double value) {
  writeValue(getKey(MARGINS_DEVIATION_THRESHOLD_KEY), value);
}

QSize ApplicationSettings::getThumbnailQuality() const {
  const QSize size = readValue(getKey(THUMBNAIL_QUALITY_KEY), DEFAULT_THUMBNAIL_QUALITY).toSize();
  return (size.width() > 0 && size.height() > 0) ? size : DEFAULT_THUMBNAIL_QUALITY;
}

void ApplicationSettings::setThumbnailQuality(const QSize& quality) {
  writeValue(getKey(THUMBNAIL_QUALITY_KEY), quality);
}

QSizeF ApplicationSettings::getMaxLogicalThumbnailSize() const {
  const QSizeF size = readValue(getKey(MAX_LOGICAL_THUMBNAIL_SIZE_KEY), DEFAULT_MAX_LOGICAL_THUMBNAIL_SIZE).toSizeF();
  return (size.width() > 0 && size.height() > 0) ? size : DEFAULT_MAX_LOGICAL_THUMBNAIL_SIZE;
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

bool ApplicationSettings::isPageLayoutShowMiddleRectEnabled() const {
  return readValue(getKey(PAGE_LAYOUT_SHOW_MIDDLE_RECT_KEY), DEFAULT_PAGE_LAYOUT_SHOW_MIDDLE_RECT).toBool();
}

void ApplicationSettings::setPageLayoutShowMiddleRectEnabled(bool enabled) {
  writeValue(getKey(PAGE_LAYOUT_SHOW_MIDDLE_RECT_KEY), enabled);
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

int ApplicationSettings::getProcessingThreads() const {
  bool ok = false;
  const int v = readValue(getKey(PROCESSING_THREADS_KEY), ThreadBudget::defaultThreads()).toInt(&ok);
  if (!ok) {
    return ThreadBudget::defaultThreads();
  }
  return qBound(1, v, ThreadBudget::availableThreads());
}

void ApplicationSettings::setProcessingThreads(const int threads) {
  const int value = qBound(1, threads, ThreadBudget::availableThreads());
  writeValue(getKey(PROCESSING_THREADS_KEY), value);
  ThreadBudget::setMaxThreads(value);
}

bool ApplicationSettings::isPdfOpenAfterCreationEnabled() const {
  return readValue(getKey(PDF_OPEN_AFTER_CREATION_KEY), DEFAULT_PDF_OPEN_AFTER_CREATION).toBool();
}

void ApplicationSettings::setPdfOpenAfterCreationEnabled(const bool enabled) {
  writeValue(getKey(PDF_OPEN_AFTER_CREATION_KEY), enabled);
}
