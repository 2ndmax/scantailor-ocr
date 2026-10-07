// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_APPLICATIONSETTINGS_H_
#define SCANTAILOR_CORE_APPLICATIONSETTINGS_H_


#include <foundation/NonCopyable.h>

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
  static ApplicationSettings& getInstance();

  bool isOpenGlEnabled() const;

  void setOpenGlEnabled(bool enabled);

  QString getColorScheme() const;

  void setColorScheme(const QString& scheme);

  bool isAutoSaveProjectEnabled() const;

  void setAutoSaveProjectEnabled(bool enabled);

  int getTiffBwCompression() const;

  void setTiffBwCompression(int compression);

  int getTiffColorCompression() const;

  void setTiffColorCompression(int compression);

  /** Compression of palette images (posterized pages), a libtiff COMPRESSION_* constant. */
  int getTiffPaletteCompression() const;

  void setTiffPaletteCompression(int compression);

  /** JPEG quality (10 to 100) for TIFF files with JPEG compression. */
  int getTiffJpegQuality() const;

  void setTiffJpegQuality(int quality);

  bool isBlackOnWhiteDetectionEnabled() const;

  void setBlackOnWhiteDetectionEnabled(bool enabled);

  bool isBlackOnWhiteDetectionOutputEnabled() const;

  void setBlackOnWhiteDetectionOutputEnabled(bool enabled);

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

  /**
   * Distance of the deskew and oblique drag handles from the image center, in percent of the
   * largest possible distance (handles at the edge of the view).  Smaller values move the
   * handles away from the edge, at the cost of a coarser angle per pixel of mouse movement.
   */
  int getDeskewHandleDistance() const;

  void setDeskewHandleDistance(int percent);

  static const int MIN_DESKEW_HANDLE_DISTANCE;
  static const int MAX_DESKEW_HANDLE_DISTANCE;

  /** PDF export: compression of grayscale / colour pages and backgrounds, a PdfCompression value. */
  int getPdfColorCompression() const;

  void setPdfColorCompression(int compression);

  /** PDF export: quality (10 to 100) of grayscale / colour pages and backgrounds. */
  int getPdfColorQuality() const;

  void setPdfColorQuality(int quality);

  /** PDF export: the background of split pages is stored at 1 / scale of the output resolution (1 to 3). */
  int getPdfBackgroundScale() const;

  void setPdfBackgroundScale(int scale);

  /** PDF export: compression of posterized pages and backgrounds, a PdfCompression value. */
  int getPdfPaletteCompression() const;

  void setPdfPaletteCompression(int compression);

  /** PDF export: quality (10 to 100) of posterized pages and backgrounds. */
  int getPdfPaletteQuality() const;

  void setPdfPaletteQuality(int quality);

  /** PDF export: compression of black and white images, a PdfCompression value. */
  int getPdfBitonalCompression() const;

  void setPdfBitonalCompression(int compression);

  /** PDF export: open the PDF in the default viewer once it's created. */
  bool isPdfOpenAfterCreationEnabled() const;

  void setPdfOpenAfterCreationEnabled(bool enabled);

  /** PDF export: add an invisible text layer by text recognition (OCR). */
  bool isPdfOcrEnabled() const;

  void setPdfOcrEnabled(bool enabled);

  /** PDF export: the OCR language codes, e.g. {"deu", "eng"}. */
  QStringList getPdfOcrLanguages() const;

  void setPdfOcrLanguages(const QStringList& languages);

  /** PDF export: the OCR page layout, see OcrEngine::PageLayout (0 to 2). */
  int getPdfOcrPageLayout() const;

  void setPdfOcrPageLayout(int layout);

 private:
  static inline QString getKey(const QString& keyName);

  static const bool DEFAULT_OPENGL_STATE;
  static const QString DEFAULT_COLOR_SCHEME;
  static const bool DEFAULT_AUTO_SAVE_PROJECT;
  static const int DEFAULT_TIFF_BW_COMPRESSION;
  static const int DEFAULT_TIFF_COLOR_COMPRESSION;
  static const int DEFAULT_TIFF_PALETTE_COMPRESSION;
  static const int DEFAULT_TIFF_JPEG_QUALITY;
  static const bool DEFAULT_BLACK_ON_WHITE_DETECTION;
  static const bool DEFAULT_BLACK_ON_WHITE_DETECTION_OUTPUT;
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
  static const QString TIFF_BW_COMPRESSION_KEY;
  static const QString TIFF_COLOR_COMPRESSION_KEY;
  static const QString TIFF_PALETTE_COMPRESSION_KEY;
  static const QString TIFF_JPEG_QUALITY_KEY;
  static const QString BLACK_ON_WHITE_DETECTION_KEY;
  static const QString BLACK_ON_WHITE_DETECTION_OUTPUT_KEY;
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

  static const int DEFAULT_ZONE_CREATION_MODE;  // 0 = polygonal
  static const bool DEFAULT_OUTPUT_SHOW_GUIDES;
  static const int DEFAULT_DESKEW_HANDLE_DISTANCE;

  static const QString PDF_JPEG_QUALITY_KEY;
  static const QString PDF_BACKGROUND_SCALE_KEY;
  static const QString PDF_OPEN_AFTER_CREATION_KEY;
  static const QString PDF_JBIG2_KEY;
  static const QString PDF_COLOR_COMPRESSION_KEY;
  static const QString PDF_PALETTE_COMPRESSION_KEY;
  static const QString PDF_PALETTE_QUALITY_KEY;
  static const QString PDF_BITONAL_COMPRESSION_KEY;
  static const int DEFAULT_PDF_JPEG_QUALITY;
  static const int DEFAULT_PDF_BACKGROUND_SCALE;
  static const bool DEFAULT_PDF_OPEN_AFTER_CREATION;
  static const bool DEFAULT_PDF_JBIG2;
  static const QString PDF_OCR_ENABLED_KEY;
  static const QString PDF_OCR_LANGUAGES_KEY;
  static const QString PDF_OCR_PAGE_LAYOUT_KEY;

  QVariant readValue(const QString& key, const QVariant& defaultValue) const;

  void writeValue(const QString& key, const QVariant& value);

  // QSettings is only reentrant, not thread-safe.  But the settings are read
  // from worker threads as well (e.g. by TiffWriter while batch processing),
  // so all access to m_settings is serialized.
  mutable QMutex m_mutex;
  QSettings m_settings;
};


#endif  // SCANTAILOR_CORE_APPLICATIONSETTINGS_H_
