// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_OCRPANEL_H_
#define SCANTAILOR_APP_OCRPANEL_H_

#include <QStringList>
#include <QWidget>

#include "ProcessingOptions.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;

/**
 * The contents of the Text Recognition (OCR) panel: on or off, the languages and the text
 * layout. The PDF step and the default parameters put it into their group boxes.
 */
class OcrPanel : public QWidget {
  Q_OBJECT
 public:
  explicit OcrPanel(QWidget* parent = nullptr);

  ~OcrPanel() override;

  /** Sets the text recognition fields of \p pdf. Doesn't emit optionsChanged(). */
  void setOptions(const ProcessingOptions::Pdf& pdf);

  /** Stores the text recognition fields in \p pdf; the others are kept. */
  void storeOptions(ProcessingOptions::Pdf& pdf) const;

  bool isOcrEnabled() const;

  /** The codes of the ticked languages that are installed. */
  QStringList checkedLanguages() const;

  int pageLayout() const;

 signals:
  void optionsChanged();

 private:
  /** Opens the dialog for downloading languages. */
  void downloadLanguages();

  /** Lists the installed languages and ticks those in \p checked. */
  void fillLanguageList(const QStringList& checked);

  /** Shows up to 8 languages; more can be scrolled. */
  void updateLanguageListHeight();

  QCheckBox* m_ocrEnabled = nullptr;
  /** Languages and text layout, greyed out while text recognition is off. */
  QWidget* m_ocrOptions = nullptr;
  QListWidget* m_languageList = nullptr;
  QLabel* m_noLanguagesLabel = nullptr;
  QComboBox* m_pageLayout = nullptr;
  /** Languages that were chosen but aren't installed here; they are kept. */
  QStringList m_missingLanguages;
};

#endif  // SCANTAILOR_APP_OCRPANEL_H_
