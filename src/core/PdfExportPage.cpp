// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportPage.h"

#include <tiff.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QSize>

#include "PdfImageEncoder.h"

PdfExportPage::PdfExportPage(const QString& mainFile,
                             const QString& foregroundFile,
                             const QString& backgroundFile,
                             const bool mixedMode)
    : m_mainFile(mainFile),
      m_foregroundFile(foregroundFile),
      m_backgroundFile(backgroundFile),
      m_mixedMode(mixedMode) {}

void PdfExportPage::analyze() {
  m_kind = MISSING;
  m_palettePicture = false;
  m_pictureCompression = 0;
  m_textCompression = 0;
  m_withoutSplitOutput = false;
  m_warning.clear();

  const QFileInfo mainInfo(m_mainFile);
  QSize mainSize;
  int mainBits = 0;
  bool mainPalette = false;
  int mainCompression = 0;
  if (!mainInfo.isFile()
      || !PdfImageEncoder::readTiffInfo(m_mainFile, &mainSize, &mainBits, &mainPalette, &mainCompression)) {
    return;
  }
  m_pictureCompression = mainCompression;
  if (mainBits == 1) {
    m_kind = BITONAL;
    return;
  }

  // The split output layers are written shortly before the output file itself.
  // Anything noticeably older belongs to an earlier version of the page.
  const int toleranceSecs = 120;
  const QDateTime oldestAllowed = mainInfo.lastModified().addSecs(-toleranceSecs);

  const QFileInfo fgInfo(m_foregroundFile);
  const QFileInfo bgInfo(m_backgroundFile);
  const bool splitFilesExist = fgInfo.isFile() && bgInfo.isFile();
  if (splitFilesExist) {
    QSize fgSize;
    QSize bgSize;
    int fgBits = 0;
    int bgBits = 0;
    bool bgPalette = false;
    int fgCompression = 0;
    int bgCompression = 0;
    const bool usable = (fgInfo.lastModified() >= oldestAllowed) && (bgInfo.lastModified() >= oldestAllowed)
                        && PdfImageEncoder::readTiffInfo(m_foregroundFile, &fgSize, &fgBits, nullptr, &fgCompression)
                        && PdfImageEncoder::readTiffInfo(m_backgroundFile, &bgSize, &bgBits, &bgPalette, &bgCompression)
                        && (fgBits == 1) && (fgSize == mainSize) && (bgSize == mainSize);
    if (usable) {
      m_kind = MRC;
      m_palettePicture = bgPalette;
      m_pictureCompression = bgCompression;
      m_textCompression = fgCompression;
      return;
    }
  }

  m_kind = mainPalette ? PALETTE : IMAGE;
  if (splitFilesExist) {
    m_warning = QCoreApplication::translate(
        "PdfExportPage",
        "The split output files of this page don't match its output file (they are older, have a different size "
        "or a colored foreground). The page is stored as a single image. Process the page again to fix this.");
  } else {
    // How much this matters depends on the PDF compression, so the PDF step tells it.
    m_withoutSplitOutput = m_mixedMode;
  }
}  // PdfExportPage::analyze

bool PdfExportPage::isLossyTiffCompression(const int compression) {
  switch (compression) {
    case COMPRESSION_NONE:
    case COMPRESSION_CCITTRLE:
    case COMPRESSION_CCITTFAX3:
    case COMPRESSION_CCITTFAX4:
    case COMPRESSION_LZW:
    case COMPRESSION_ADOBE_DEFLATE:
    case COMPRESSION_CCITTRLEW:
    case COMPRESSION_PACKBITS:
    case COMPRESSION_DEFLATE:
    case COMPRESSION_LZMA:
    case COMPRESSION_ZSTD:
      return false;
    default:
      // JPEG, and methods this program doesn't write.
      return true;
  }
}

QString PdfExportPage::tiffCompressionName(const int compression) {
  switch (compression) {
    case COMPRESSION_NONE:
      return QCoreApplication::translate("PdfExportPage", "uncompressed");
    case COMPRESSION_LZW:
      return QStringLiteral("LZW");
    case COMPRESSION_ADOBE_DEFLATE:
    case COMPRESSION_DEFLATE:
      return QStringLiteral("Deflate");
    case COMPRESSION_LZMA:
      return QStringLiteral("LZMA");
    case COMPRESSION_OJPEG:
    case COMPRESSION_JPEG:
      return QStringLiteral("JPEG");
    case COMPRESSION_CCITTFAX4:
      return QStringLiteral("CCITT G4");
    case COMPRESSION_CCITTFAX3:
      return QStringLiteral("CCITT G3");
    case COMPRESSION_PACKBITS:
      return QStringLiteral("PackBits");
    case COMPRESSION_ZSTD:
      return QStringLiteral("ZSTD");
    default:
      return QCoreApplication::translate("PdfExportPage", "method %1").arg(compression);
  }
}
