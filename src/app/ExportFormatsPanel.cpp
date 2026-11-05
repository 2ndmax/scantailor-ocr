// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ExportFormatsPanel.h"

#include <QCheckBox>
#include <QSignalBlocker>
#include <QVBoxLayout>

ExportFormatsPanel::ExportFormatsPanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_pdf = new QCheckBox(QStringLiteral("PDF"));
  m_pdf->setToolTip(tr("Creates a PDF file with the pages as images, in the order of the export."));
  layout->addWidget(m_pdf);

  // Indented below the PDF, as they only concern it.
  m_pdfOptions = new QWidget;
  auto* pdfLayout = new QVBoxLayout(m_pdfOptions);
  pdfLayout->setContentsMargins(15, 0, 0, 0);
  m_pdfA = new QCheckBox(QStringLiteral("PDF/A-2b"));
  m_pdfA->setToolTip(
      tr("Creates the PDF as PDF/A-2b, the ISO standard for long-term archiving. The file then also contains a "
         "color profile and metadata, so it will look the same in future PDF programs. Adobe Acrobat opens "
         "such files read-only at first; editing has to be enabled there."));
  pdfLayout->addWidget(m_pdfA);
  m_textLayer = new QCheckBox(tr("Text layer (searchable)"));
  m_textLayer->setToolTip(
      tr("Puts the recognized text invisibly over the pages, so it can be searched, selected and copied in the "
         "PDF. Text recognition takes a few seconds per page."));
  pdfLayout->addWidget(m_textLayer);
  layout->addWidget(m_pdfOptions);

  m_txt = new QCheckBox(tr("Text (TXT)"));
  m_txt->setToolTip(
      tr("Creates a text file with the recognized text of all pages, in the order of the export. A page break "
         "(form feed) separates the pages."));
  layout->addWidget(m_txt);
  m_hocr = new QCheckBox(QStringLiteral("hOCR"));
  m_hocr->setToolTip(
      tr("Creates an hOCR file (HTML) with the recognized text of all pages and the place of each word on the "
         "page, for programs that process recognized text."));
  layout->addWidget(m_hocr);
  m_alto = new QCheckBox(tr("ALTO (one file per page)"));
  m_alto->setToolTip(
      tr("Creates a folder with an ALTO file (XML) for each page, with the recognized text and the place of each "
         "word on the page. Libraries and archives use this format."));
  layout->addWidget(m_alto);

  setOptions(ProcessingOptions());

  for (QCheckBox* box : {m_pdf, m_pdfA, m_textLayer, m_txt, m_hocr, m_alto}) {
    connect(box, &QCheckBox::toggled, this, &ExportFormatsPanel::optionsChanged);
  }
  connect(m_pdf, &QCheckBox::toggled, this, &ExportFormatsPanel::updatePdfOptions);
}

ExportFormatsPanel::~ExportFormatsPanel() = default;

void ExportFormatsPanel::setOptions(const ProcessingOptions& options) {
  const QSignalBlocker pdfBlocker(m_pdf);
  const QSignalBlocker pdfABlocker(m_pdfA);
  const QSignalBlocker textLayerBlocker(m_textLayer);
  const QSignalBlocker txtBlocker(m_txt);
  const QSignalBlocker hocrBlocker(m_hocr);
  const QSignalBlocker altoBlocker(m_alto);
  m_pdf->setChecked(options.formats.pdf);
  m_pdfA->setChecked(options.pdf.pdfA);
  m_textLayer->setChecked(options.pdf.textLayer);
  m_txt->setChecked(options.formats.txt);
  m_hocr->setChecked(options.formats.hocr);
  m_alto->setChecked(options.formats.alto);
  updatePdfOptions();
}

void ExportFormatsPanel::storeOptions(ProcessingOptions& options) const {
  options.formats.pdf = m_pdf->isChecked();
  options.pdf.pdfA = m_pdfA->isChecked();
  options.pdf.textLayer = m_textLayer->isChecked();
  options.formats.txt = m_txt->isChecked();
  options.formats.hocr = m_hocr->isChecked();
  options.formats.alto = m_alto->isChecked();
}

void ExportFormatsPanel::updatePdfOptions() {
  m_pdfOptions->setEnabled(m_pdf->isChecked());
}
