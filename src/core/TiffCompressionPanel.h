// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TIFFCOMPRESSIONPANEL_H_
#define SCANTAILOR_CORE_TIFFCOMPRESSIONPANEL_H_

#include <QWidget>

#include "TiffWriter.h"

class QComboBox;
class QFormLayout;
class QSpinBox;

/**
 * The contents of the TIFF Compression panel: a method for each kind of page and the JPEG
 * quality. The Output step and the default parameters put it into their group boxes.
 */
class TiffCompressionPanel : public QWidget {
  Q_OBJECT
 public:
  explicit TiffCompressionPanel(QWidget* parent = nullptr);

  ~TiffCompressionPanel() override;

  TiffWriter::Compression compression() const;

  /** Doesn't emit compressionChanged(). */
  void setCompression(const TiffWriter::Compression& compression);

 signals:
  /** \p fromSpinBox: the JPEG quality changed, which happens step by step. */
  void compressionChanged(bool fromSpinBox);

 private:
  QComboBox* m_colorCompression = nullptr;
  QSpinBox* m_jpegQuality = nullptr;
  QComboBox* m_paletteCompression = nullptr;
  QComboBox* m_bwCompression = nullptr;
};

#endif  // SCANTAILOR_CORE_TIFFCOMPRESSIONPANEL_H_
