// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_OCRPANEL_H_
#define SCANTAILOR_APP_OCRPANEL_H_

#include <QStringList>
#include <QWidget>

#include "ProcessingOptions.h"

class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

/**
 * The contents of the Text Recognition (OCR) panel: the languages and the text layout. Whether
 * the text is recognized at all follows from the formats (see ExportFormatsPanel). The Export
 * step and the default parameters put it into their group boxes.
 */
class OcrPanel : public QWidget {
  Q_OBJECT
 public:
  explicit OcrPanel(QWidget* parent = nullptr);

  ~OcrPanel() override;

  /** Shows the values of \p ocr. Doesn't emit optionsChanged(). */
  void setOptions(const ProcessingOptions::Ocr& ocr);

  void storeOptions(ProcessingOptions::Ocr& ocr) const;

  /** Whether a chosen format needs the text; then a warning shows while no language is ticked. */
  void setTextNeeded(bool needed);

  /** Whether a language is ticked, also one whose file is missing on this computer. */
  bool hasTickedLanguage() const;

  /** The codes of the ticked languages that are installed. */
  QStringList checkedLanguages() const;

  int pageLayout() const;

  /**
   * The chosen languages whose files are missing on this computer. They are listed ticked, with a
   * warning sign, and kept in the project.
   */
  QStringList missingLanguages() const;

  /** Their names, separated by commas. */
  QString missingLanguageNames() const;

  /** Opens the dialog for downloading languages with the missing ones ticked. */
  void downloadMissingLanguages();

 signals:
  void optionsChanged();

 private:
  /** Item data: whether the language is missing on this computer. */
  static constexpr int MISSING_ROLE = Qt::UserRole + 1;

  /** Opens the dialog for downloading languages, with \p preselected ticked. */
  void downloadLanguages(const QStringList& preselected);

  /** The codes of the ticked languages, with or without the missing ones. */
  QStringList tickedLanguages(bool withMissing) const;

  /** A missing language that is unticked leaves the list. */
  void languageChanged(QListWidgetItem* item);

  /** Lists the installed languages and the missing ones of \p checked, and ticks those in \p checked. */
  void fillLanguageList(const QStringList& checked);

  /** Shows up to 8 languages; more can be scrolled. */
  void updateLanguageListHeight();

  void updateLanguageWarning();

  QListWidget* m_languageList = nullptr;
  QLabel* m_noLanguagesLabel = nullptr;
  QLabel* m_noLanguageTickedLabel = nullptr;
  QComboBox* m_pageLayout = nullptr;
  bool m_textNeeded = false;
};

#endif  // SCANTAILOR_APP_OCRPANEL_H_
