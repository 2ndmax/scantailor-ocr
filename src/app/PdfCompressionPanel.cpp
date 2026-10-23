// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfCompressionPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>

#include "SectionHeading.h"

namespace {
/** Whether the method chosen in a compression box uses the quality setting. */
bool usesQuality(const QComboBox* box) {
  const auto compression = static_cast<PdfCompression>(box->currentData().toInt());
  return (compression == PdfCompression::JPEG) || (compression == PdfCompression::JPEG2000);
}

// A stored value that isn't offered falls back to the default.
void selectData(QComboBox* box, const PdfCompression value, const PdfCompression defaultValue) {
  int index = box->findData(static_cast<int>(value));
  if (index < 0) {
    index = box->findData(static_cast<int>(defaultValue));
  }
  box->setCurrentIndex(std::max(0, index));
}
}  // namespace

PdfCompressionPanel::PdfCompressionPanel(QWidget* parent) : QWidget(parent) {
  auto* optionsLayout = new QFormLayout(this);
  optionsLayout->setContentsMargins(0, 0, 0, 0);
  // All lists start after the longest label and keep their width instead of filling a widened
  // panel. The rows don't wrap: the panel is at least as wide as the longest row anyway.
  optionsLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
  optionsLayout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  // The group has a part for each kind of page, each with a bold heading.
  auto addHeading = [optionsLayout](const QString& text, const QString& toolTip, const bool first) {
    auto* heading = new SectionHeading(text);
    heading->setToolTip(toolTip);
    heading->setFirst(first);
    optionsLayout->addRow(heading);
  };
  const QString qualityToolTip = tr(
      "Higher values give better pictures and larger files. Only used with JPEG and JPEG 2000. With "
      "JPEG 2000, 100 is lossless; with JPEG, it's only nearly lossless.");

  // Color and grayscale.
  addHeading(tr("Color / Grayscale"),
             tr("Color and grayscale pages, also posterized grayscale pages, and the pictures of pages with split "
                "output."),
             true);
  m_colorCompression = new QComboBox;
  m_colorCompression->addItem(tr("None"), static_cast<int>(PdfCompression::NONE));
  m_colorCompression->addItem(QStringLiteral("Deflate"), static_cast<int>(PdfCompression::DEFLATE));
  m_colorCompression->addItem(QStringLiteral("JPEG"), static_cast<int>(PdfCompression::JPEG));
  m_colorCompression->addItem(QStringLiteral("JPEG 2000"), static_cast<int>(PdfCompression::JPEG2000));
  m_colorCompression->setToolTip(
      tr("JPEG makes small files and loses a little quality, mostly at the edges of text. JPEG 2000 looks "
         "better at the same size, or makes smaller files at the same quality, but takes much longer to create, "
         "and some simple or old PDF programs can't show it. Deflate is lossless, but makes the PDF very large: "
         "a color page can take 10 to 25 MB, uncompressed (None) up to 45 MB."));
  optionsLayout->addRow(tr("Method:"), m_colorCompression);

  m_colorQuality = new QSpinBox;
  m_colorQuality->setRange(10, 100);
  m_colorQuality->setToolTip(qualityToolTip);
  optionsLayout->addRow(tr("Quality:"), m_colorQuality);

  m_backgroundScale = new QComboBox;
  m_backgroundScale->addItem(tr("Full"), 1);
  m_backgroundScale->addItem(tr("Half"), 2);
  m_backgroundScale->addItem(tr("One third"), 3);
  m_backgroundScale->setToolTip(
      tr("Resolution of the background layer of pages with split output, that is of the pictures, relative to "
         "the output resolution. The text is always stored at full resolution."));
  auto* backgroundScaleLabel = new QLabel(tr("Background layer:"));
  backgroundScaleLabel->setToolTip(m_backgroundScale->toolTip());
  optionsLayout->addRow(backgroundScaleLabel, m_backgroundScale);

  // Posterized pages.
  addHeading(tr("Posterized Pages"),
             tr("Color pages that were posterized in the Output step, and posterized pictures of pages with split "
                "output. Posterized grayscale pages can't be told apart from other grayscale pages; they follow "
                "\"Color / Grayscale\"."),
             false);
  m_paletteCompression = new QComboBox;
  m_paletteCompression->addItem(tr("None"), static_cast<int>(PdfCompression::NONE));
  m_paletteCompression->addItem(QStringLiteral("Deflate"), static_cast<int>(PdfCompression::DEFLATE));
  m_paletteCompression->addItem(QStringLiteral("JPEG"), static_cast<int>(PdfCompression::JPEG));
  m_paletteCompression->addItem(QStringLiteral("JPEG 2000"), static_cast<int>(PdfCompression::JPEG2000));
  m_paletteCompression->setToolTip(
      tr("Deflate is lossless and keeps the few colors exactly, which makes small and sharp files. JPEG and "
         "JPEG 2000 convert the page to full color first, which blurs the color areas and often makes the file "
         "larger. None is lossless, but large."));
  optionsLayout->addRow(tr("Method:"), m_paletteCompression);

  m_paletteQuality = new QSpinBox;
  m_paletteQuality->setRange(10, 100);
  m_paletteQuality->setToolTip(qualityToolTip);
  optionsLayout->addRow(tr("Quality:"), m_paletteQuality);

  // Black and white.
  addHeading(tr("Black and White"), tr("Black and white pages and the text of pages with split output."), false);
  m_bitonalCompression = new QComboBox;
  m_bitonalCompression->addItem(tr("None"), static_cast<int>(PdfCompression::NONE));
  m_bitonalCompression->addItem(QStringLiteral("Deflate"), static_cast<int>(PdfCompression::DEFLATE));
  m_bitonalCompression->addItem(QStringLiteral("CCITT G4"), static_cast<int>(PdfCompression::CCITT_G4));
  m_bitonalCompression->addItem(QStringLiteral("JBIG2"), static_cast<int>(PdfCompression::JBIG2));
  m_bitonalCompression->setToolTip(
      tr("All methods are lossless. JBIG2 makes the smallest files. CCITT G4 files are about a third larger, but "
         "can be shown by very old PDF programs, too. Deflate and None make much larger files."));
  optionsLayout->addRow(tr("Method:"), m_bitonalCompression);

  // Archiving.
  addHeading(tr("Archiving"), QString(), false);
  m_pdfA = new QCheckBox(QStringLiteral("PDF/A-2b"));
  m_pdfA->setToolTip(
      tr("Creates the PDF as PDF/A-2b, the ISO standard for long-term archiving. The file then also contains a "
         "color profile and metadata, so it will look the same in future PDF programs. Adobe Acrobat opens "
         "such files read-only at first; editing has to be enabled there."));
  optionsLayout->addRow(m_pdfA);

  // The lists of the group line up.
  int listWidth = 0;
  for (QComboBox* box : {m_colorCompression, m_backgroundScale, m_paletteCompression, m_bitonalCompression}) {
    listWidth = std::max(listWidth, box->sizeHint().width());
  }
  for (QComboBox* box : {m_colorCompression, m_backgroundScale, m_paletteCompression, m_bitonalCompression}) {
    box->setFixedWidth(listWidth);
  }

  setOptions(ProcessingOptions::Pdf());

  for (QComboBox* box : {m_colorCompression, m_backgroundScale, m_paletteCompression, m_bitonalCompression}) {
    connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
      updateQualityFields();
      emit compressionChanged();
    });
  }
  // JPEG 2000 is lossless at quality 100, so the quality matters for the warnings, too.
  for (QSpinBox* box : {m_colorQuality, m_paletteQuality}) {
    connect(box, qOverload<int>(&QSpinBox::valueChanged), this, &PdfCompressionPanel::compressionChanged);
  }
  connect(m_pdfA, &QCheckBox::toggled, this, &PdfCompressionPanel::compressionChanged);
}

PdfCompressionPanel::~PdfCompressionPanel() = default;

void PdfCompressionPanel::setOptions(const ProcessingOptions::Pdf& pdf) {
  const QSignalBlocker colorBlocker(m_colorCompression);
  const QSignalBlocker colorQualityBlocker(m_colorQuality);
  const QSignalBlocker backgroundBlocker(m_backgroundScale);
  const QSignalBlocker paletteBlocker(m_paletteCompression);
  const QSignalBlocker paletteQualityBlocker(m_paletteQuality);
  const QSignalBlocker bitonalBlocker(m_bitonalCompression);
  const QSignalBlocker pdfABlocker(m_pdfA);
  const ProcessingOptions::Pdf defaults;
  selectData(m_colorCompression, pdf.colorCompression, defaults.colorCompression);
  m_colorQuality->setValue(pdf.colorQuality);
  m_backgroundScale->setCurrentIndex(std::max(0, m_backgroundScale->findData(pdf.backgroundScale)));
  selectData(m_paletteCompression, pdf.paletteCompression, defaults.paletteCompression);
  m_paletteQuality->setValue(pdf.paletteQuality);
  selectData(m_bitonalCompression, pdf.bitonalCompression, defaults.bitonalCompression);
  m_pdfA->setChecked(pdf.pdfA);
  updateQualityFields();
}

void PdfCompressionPanel::storeOptions(ProcessingOptions::Pdf& pdf) const {
  pdf.colorCompression = static_cast<PdfCompression>(m_colorCompression->currentData().toInt());
  pdf.colorQuality = m_colorQuality->value();
  pdf.backgroundScale = m_backgroundScale->currentData().toInt();
  pdf.paletteCompression = static_cast<PdfCompression>(m_paletteCompression->currentData().toInt());
  pdf.paletteQuality = m_paletteQuality->value();
  pdf.bitonalCompression = static_cast<PdfCompression>(m_bitonalCompression->currentData().toInt());
  pdf.pdfA = m_pdfA->isChecked();
}

void PdfCompressionPanel::updateQualityFields() {
  m_colorQuality->setEnabled(usesQuality(m_colorCompression));
  m_paletteQuality->setEnabled(usesQuality(m_paletteCompression));
}
