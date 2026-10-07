// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportPage.h"

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
  m_warning.clear();

  const QFileInfo mainInfo(m_mainFile);
  QSize mainSize;
  int mainBits = 0;
  bool mainPalette = false;
  if (!mainInfo.isFile() || !PdfImageEncoder::readTiffInfo(m_mainFile, &mainSize, &mainBits, &mainPalette)) {
    return;
  }
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
    const bool usable = (fgInfo.lastModified() >= oldestAllowed) && (bgInfo.lastModified() >= oldestAllowed)
                        && PdfImageEncoder::readTiffInfo(m_foregroundFile, &fgSize, &fgBits)
                        && PdfImageEncoder::readTiffInfo(m_backgroundFile, &bgSize, &bgBits, &bgPalette)
                        && (fgBits == 1) && (fgSize == mainSize) && (bgSize == mainSize);
    if (usable) {
      m_kind = MRC;
      m_palettePicture = bgPalette;
      return;
    }
  }

  m_kind = mainPalette ? PALETTE : IMAGE;
  if (splitFilesExist) {
    m_warning = QCoreApplication::translate(
        "PdfExportPage",
        "The split output files of this page don't match its output file (they are older, have a different size "
        "or a colored foreground).  The page is stored as a single image.  Process the page again to fix this.");
  } else if (m_mixedMode) {
    m_warning = QCoreApplication::translate(
        "PdfExportPage",
        "Mixed page without split output.  It is stored as a single image, which makes the PDF larger and, with "
        "JPEG, the text less sharp.  For a smaller file, enable \"Split output\" in the Output stage and process "
        "the page again.");
  }
}  // PdfExportPage::analyze
