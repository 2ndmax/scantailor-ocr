// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_EXPORTFORMATSPANEL_H_
#define SCANTAILOR_APP_EXPORTFORMATSPANEL_H_

#include <QWidget>

#include "ProcessingOptions.h"

class QCheckBox;

/**
 * The contents of the Formats panel: which files the export creates, and for the PDF whether it
 * is PDF/A and has a text layer. The Export step and the default parameters put it into their
 * group boxes.
 */
class ExportFormatsPanel : public QWidget {
  Q_OBJECT
 public:
  explicit ExportFormatsPanel(QWidget* parent = nullptr);

  ~ExportFormatsPanel() override;

  /** Shows the formats, PDF/A and the text layer of \p options. Doesn't emit optionsChanged(). */
  void setOptions(const ProcessingOptions& options);

  /** Stores the formats, PDF/A and the text layer in \p options; the rest is kept. */
  void storeOptions(ProcessingOptions& options) const;

 signals:
  void optionsChanged();

 private:
  /** The options of the PDF are greyed out while no PDF is created. */
  void updatePdfOptions();

  QCheckBox* m_pdf = nullptr;
  QWidget* m_pdfOptions = nullptr;
  QCheckBox* m_pdfA = nullptr;
  QCheckBox* m_textLayer = nullptr;
  QCheckBox* m_txt = nullptr;
  QCheckBox* m_hocr = nullptr;
  QCheckBox* m_alto = nullptr;
};

#endif  // SCANTAILOR_APP_EXPORTFORMATSPANEL_H_
