// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLICATIONSETTINGS_H_
#define SCANTAILOR_CORE_APPLICATIONSETTINGS_H_


#include <foundation/NonCopyable.h>

#include <QColor>
#include <QMutex>
#include <QSettings>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QStringList>

class ApplicationSettings {
  DECLARE_NON_COPYABLE(ApplicationSettings)
 private:
  ApplicationSettings();

 public:
  // Also the fallback of ColorSchemeFactory for unknown names.
  static const QString DEFAULT_COLOR_SCHEME;

  static ApplicationSettings& getInstance();

  bool isOpenGlEnabled() const;

  void setOpenGlEnabled(bool enabled);

  QString getColorScheme() const;

  void setColorScheme(const QString& scheme);

  /** The accent color chosen by the user, or an invalid color for the default of the color scheme. */
  QColor getAccentColor() const;

  void setAccentColor(const QColor& color);

  bool isAutoSaveProjectEnabled() const;

  void setAutoSaveProjectEnabled(bool enabled);

  bool isHighlightDeviationEnabled() const;

  void setHighlightDeviationEnabled(bool enabled);

  double getDeskewDeviationCoef() const;

  void setDeskewDeviationCoef(double value);

  double getDeskewDeviationThreshold() const;

  void setDeskewDeviationThreshold(double value);

  double getSelectContentDeviationCoef() const;

  void setSelectContentDeviationCoef(double value);

  double getSelectContentDeviationThreshold() const;

  void setSelectContentDeviationThreshold(double value);

  double getMarginsDeviationCoef() const;

  void setMarginsDeviationCoef(double value);

  double getMarginsDeviationThreshold() const;

  void setMarginsDeviationThreshold(double value);

  QSize getThumbnailQuality() const;

  void setThumbnailQuality(const QSize& quality);

  QSizeF getMaxLogicalThumbnailSize() const;

  void setMaxLogicalThumbnailSize(const QSizeF& size);

  bool isSingleColumnThumbnailDisplayEnabled() const;

  void setSingleColumnThumbnailDisplayEnabled(bool enabled);

  QString getLanguage() const;

  void setLanguage(const QString& language);

  QString getUnits() const;

  void setUnits(const QString& units);

  QString getCurrentProfile() const;

  void setCurrentProfile(const QString& profile);

  bool isCancelingSelectionQuestionEnabled();

  void setCancelingSelectionQuestionEnabled(bool enabled);

  /** Default zone creation mode: 0=polygonal (Z), 1=lasso (X), 2=rectangular (C). Issue #15. */
  int getDefaultZoneCreationMode() const;

  void setDefaultZoneCreationMode(int mode);

  /** Show center guides in Output view. Issue #82. */
  bool isOutputShowGuidesEnabled() const;

  void setOutputShowGuidesEnabled(bool enabled);

  /** Show the rectangle of the margins in the Margins view; its corners then change the margins. */
  bool isPageLayoutShowMiddleRectEnabled() const;

  void setPageLayoutShowMiddleRectEnabled(bool enabled);

  /**
   * Distance of the deskew and oblique drag handles from the image center, in percent of the
   * largest possible distance (handles at the edge of the view). Smaller values move the
   * handles away from the edge, at the cost of a coarser angle per pixel of mouse movement.
   */
  int getDeskewHandleDistance() const;

  void setDeskewHandleDistance(int percent);

  static const int MIN_DESKEW_HANDLE_DISTANCE;
  static const int MAX_DESKEW_HANDLE_DISTANCE;

  /**
   * The processor threads for all parallel work (see ThreadBudget), 1 to ThreadBudget::availableThreads().
   * Without an entry ThreadBudget::defaultThreads(). Setting it also sets the ThreadBudget.
   */
  int getProcessingThreads() const;

  void setProcessingThreads(int threads);

  /** PDF export: open the PDF in the default viewer once it's created. */
  bool isPdfOpenAfterCreationEnabled() const;

  void setPdfOpenAfterCreationEnabled(bool enabled);

 private:
  static inline QString getKey(const QString& keyName);

  static const bool DEFAULT_OPENGL_STATE;
  static const bool DEFAULT_AUTO_SAVE_PROJECT;
  static const bool DEFAULT_HIGHLIGHT_DEVIATION;
  static const double DEFAULT_DESKEW_DEVIATION_COEF;
  static const double DEFAULT_DESKEW_DEVIATION_THRESHOLD;
  static const double DEFAULT_SELECT_CONTENT_DEVIATION_COEF;
  static const double DEFAULT_SELECT_CONTENT_DEVIATION_THRESHOLD;
  static const double DEFAULT_MARGINS_DEVIATION_COEF;
  static const double DEFAULT_MARGINS_DEVIATION_THRESHOLD;
  static const QSize DEFAULT_THUMBNAIL_QUALITY;
  static const QSizeF DEFAULT_MAX_LOGICAL_THUMBNAIL_SIZE;
  static const bool DEFAULT_SINGLE_COLUMN_THUMBNAIL_DISPLAY;
  static const QString DEFAULT_LANGUAGE;
  static const QString DEFAULT_UNITS;
  static const QString DEFAULT_PROFILE;
  static const bool DEFAULT_SHOW_CANCELING_SELECTION_QUESTION;

  static const QString ROOT_KEY;
  static const QString OPENGL_STATE_KEY;
  static const QString AUTO_SAVE_PROJECT_KEY;
  static const QString COLOR_SCHEME_KEY;
  static const QString ACCENT_COLOR_KEY;
  static const QString HIGHLIGHT_DEVIATION_KEY;
  static const QString DESKEW_DEVIATION_COEF_KEY;
  static const QString DESKEW_DEVIATION_THRESHOLD_KEY;
  static const QString SELECT_CONTENT_DEVIATION_COEF_KEY;
  static const QString SELECT_CONTENT_DEVIATION_THRESHOLD_KEY;
  static const QString MARGINS_DEVIATION_COEF_KEY;
  static const QString MARGINS_DEVIATION_THRESHOLD_KEY;
  static const QString THUMBNAIL_QUALITY_KEY;
  static const QString MAX_LOGICAL_THUMBNAIL_SIZE_KEY;
  static const QString SINGLE_COLUMN_THUMBNAIL_DISPLAY_KEY;
  static const QString LANGUAGE_KEY;
  static const QString UNITS_KEY;
  static const QString CURRENT_PROFILE_KEY;
  static const QString SHOW_CANCELING_SELECTION_QUESTION_KEY;
  static const QString DEFAULT_ZONE_CREATION_MODE_KEY;
  static const QString OUTPUT_SHOW_GUIDES_KEY;
  static const QString DESKEW_HANDLE_DISTANCE_KEY;
  static const QString PROCESSING_THREADS_KEY;

  static const int DEFAULT_ZONE_CREATION_MODE;  // 0 = polygonal
  static const bool DEFAULT_OUTPUT_SHOW_GUIDES;
  static const QString PAGE_LAYOUT_SHOW_MIDDLE_RECT_KEY;
  static const bool DEFAULT_PAGE_LAYOUT_SHOW_MIDDLE_RECT;
  static const int DEFAULT_DESKEW_HANDLE_DISTANCE;

  static const QString PDF_OPEN_AFTER_CREATION_KEY;
  static const bool DEFAULT_PDF_OPEN_AFTER_CREATION;

  QVariant readValue(const QString& key, const QVariant& defaultValue) const;

  void writeValue(const QString& key, const QVariant& value);

  // QSettings is only reentrant, not thread-safe. But the settings are read
  // from worker threads as well,
  // so all access to m_settings is serialized.
  mutable QMutex m_mutex;
  QSettings m_settings;
};


#endif  // SCANTAILOR_CORE_APPLICATIONSETTINGS_H_
