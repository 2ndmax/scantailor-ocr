// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TiffCompressionPanel.h"

#include <tiff.h>

#include <QComboBox>
#include <QFormLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <algorithm>

#include "SectionHeading.h"
#include "Utils.h"

namespace {
// A stored value that isn't offered falls back to the default, as the writer would get it.
void selectData(QComboBox* box, const int value, const int defaultValue) {
  int index = box->findData(value);
  if (index < 0) {
    index = box->findData(defaultValue);
  }
  box->setCurrentIndex(std::max(0, index));
}
}  // namespace

TiffCompressionPanel::TiffCompressionPanel(QWidget* parent) : QWidget(parent) {
  auto* layout = new QFormLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  // The lists keep their width instead of filling a widened panel, as in the PDF step.
  layout->setRowWrapPolicy(QFormLayout::DontWrapRows);
  layout->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  // A part for each kind of page, each with a bold heading.
  auto addHeading = [layout](const QString& text, const QString& toolTip, const bool first) {
    auto* heading = new SectionHeading(text);
    heading->setToolTip(toolTip);
    heading->setFirst(first);
    layout->addRow(heading);
  };
  const QString lzmaNote = tr(
      "LZMA makes the smallest lossless files, but is slow, and few other programs can read it; this program "
      "and its PDF export can.");

  // Color and grayscale.
  addHeading(tr("Color / Grayscale"),
             tr("Color and grayscale pages, also posterized grayscale pages, and the pictures of pages with split "
                "output."),
             true);
  m_colorCompression = new QComboBox;
  m_colorCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_colorCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_colorCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_colorCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  m_colorCompression->addItem(QStringLiteral("JPEG"), COMPRESSION_JPEG);
  m_colorCompression->setToolTip(
      tr("All methods but JPEG are lossless; Deflate usually gives smaller files than LZW.") + QStringLiteral("  ")
      + lzmaNote + QStringLiteral("  ")
      + tr("JPEG gives much smaller files, but loses quality: the PDF compresses the pictures a second time, and "
           "on mixed pages without split output the text gets blurred."));
  // The fields start right after the longest label of the group, the same for all parts.
  layout->addRow(tr("Method:"), m_colorCompression);

  m_jpegQuality = new QSpinBox;
  m_jpegQuality->setRange(TiffWriter::Compression::MIN_JPEG_QUALITY, TiffWriter::Compression::MAX_JPEG_QUALITY);
  core::Utils::setNumberFieldWidth(m_jpegQuality, -100, 0);
  m_jpegQuality->setToolTip(
      tr("Higher values give better pictures and larger files. Only used with JPEG compression."));
  layout->addRow(tr("Quality:"), m_jpegQuality);

  // Posterized pages.
  addHeading(tr("Posterized Pages"),
             tr("Color pages that were posterized, and posterized pictures of pages with split output. Posterized "
                "grayscale pages can't be told apart from other grayscale pages; they follow \"Color / "
                "Grayscale\"."),
             false);
  m_paletteCompression = new QComboBox;
  m_paletteCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_paletteCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_paletteCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_paletteCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  m_paletteCompression->setToolTip(tr("All methods are lossless.") + QStringLiteral("  ") + lzmaNote);
  layout->addRow(tr("Method:"), m_paletteCompression);

  // Black and white.
  addHeading(tr("Black and White"), tr("Black and white pages and the text of pages with split output."), false);
  m_bwCompression = new QComboBox;
  m_bwCompression->addItem(tr("None"), COMPRESSION_NONE);
  m_bwCompression->addItem(QStringLiteral("LZW"), COMPRESSION_LZW);
  m_bwCompression->addItem(QStringLiteral("Deflate"), COMPRESSION_DEFLATE);
  m_bwCompression->addItem(QStringLiteral("LZMA"), COMPRESSION_LZMA);
  m_bwCompression->addItem(QStringLiteral("CCITT G4"), COMPRESSION_CCITTFAX4);
  m_bwCompression->setToolTip(tr("All methods are lossless; CCITT G4 usually gives the smallest files.")
                              + QStringLiteral("  ") + lzmaNote);
  layout->addRow(tr("Method:"), m_bwCompression);

  // The lists of the three parts line up.
  int listWidth = 0;
  for (QComboBox* box : {m_colorCompression, m_paletteCompression, m_bwCompression}) {
    listWidth = std::max(listWidth, box->sizeHint().width());
  }
  for (QComboBox* box : {m_colorCompression, m_paletteCompression, m_bwCompression}) {
    box->setFixedWidth(listWidth);
  }

  setCompression(TiffWriter::Compression());

  for (QComboBox* box : {m_colorCompression, m_paletteCompression, m_bwCompression}) {
    connect(box, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
      m_jpegQuality->setEnabled(m_colorCompression->currentData().toInt() == COMPRESSION_JPEG);
      emit compressionChanged(false);
    });
  }
  connect(m_jpegQuality, qOverload<int>(&QSpinBox::valueChanged), this, [this]() { emit compressionChanged(true); });
}

TiffCompressionPanel::~TiffCompressionPanel() = default;

TiffWriter::Compression TiffCompressionPanel::compression() const {
  TiffWriter::Compression compression;
  compression.bw = m_bwCompression->currentData().toInt();
  compression.color = m_colorCompression->currentData().toInt();
  compression.jpegQuality = m_jpegQuality->value();
  compression.palette = m_paletteCompression->currentData().toInt();
  return compression;
}

void TiffCompressionPanel::setCompression(const TiffWriter::Compression& compression) {
  const QSignalBlocker colorBlocker(m_colorCompression);
  const QSignalBlocker qualityBlocker(m_jpegQuality);
  const QSignalBlocker paletteBlocker(m_paletteCompression);
  const QSignalBlocker bwBlocker(m_bwCompression);
  const TiffWriter::Compression defaults;
  selectData(m_colorCompression, compression.color, defaults.color);
  m_jpegQuality->setValue(compression.jpegQuality);
  selectData(m_paletteCompression, compression.palette, defaults.palette);
  selectData(m_bwCompression, compression.bw, defaults.bw);
  m_jpegQuality->setEnabled(m_colorCompression->currentData().toInt() == COMPRESSION_JPEG);
}
