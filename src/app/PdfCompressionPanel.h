// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PDFCOMPRESSIONPANEL_H_
#define SCANTAILOR_APP_PDFCOMPRESSIONPANEL_H_

#include <QWidget>

#include "ProcessingOptions.h"

class QCheckBox;
class QComboBox;
class QSpinBox;

/**
 * The contents of the PDF Compression panel: a method for each kind of page, the qualities, the
 * resolution of the background layer and PDF/A. The PDF step and the default parameters put it
 * into their group boxes.
 */
class PdfCompressionPanel : public QWidget {
  Q_OBJECT
 public:
  explicit PdfCompressionPanel(QWidget* parent = nullptr);

  ~PdfCompressionPanel() override;

  /** Sets the compression fields of \p pdf. Doesn't emit compressionChanged(). */
  void setOptions(const ProcessingOptions::Pdf& pdf);

  /** Stores the compression fields in \p pdf; the others are kept. */
  void storeOptions(ProcessingOptions::Pdf& pdf) const;

  // The fields, for the Quality panel of the PDF step.
  QComboBox* colorCompressionBox() const { return m_colorCompression; }

  QSpinBox* colorQualityBox() const { return m_colorQuality; }

  QComboBox* paletteCompressionBox() const { return m_paletteCompression; }

  QSpinBox* paletteQualityBox() const { return m_paletteQuality; }

  QComboBox* bitonalCompressionBox() const { return m_bitonalCompression; }

 signals:
  void compressionChanged();

 private:
  /** Greys out the quality fields that the chosen methods don't use. */
  void updateQualityFields();

  QComboBox* m_colorCompression = nullptr;
  QSpinBox* m_colorQuality = nullptr;
  QComboBox* m_backgroundScale = nullptr;
  QComboBox* m_paletteCompression = nullptr;
  QSpinBox* m_paletteQuality = nullptr;
  QComboBox* m_bitonalCompression = nullptr;
  QCheckBox* m_pdfA = nullptr;
};

#endif  // SCANTAILOR_APP_PDFCOMPRESSIONPANEL_H_
