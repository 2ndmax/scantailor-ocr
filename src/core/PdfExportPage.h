// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFEXPORTPAGE_H_
#define SCANTAILOR_CORE_PDFEXPORTPAGE_H_

#include <QString>
#include <set>
#include <vector>

#include "PageId.h"

/**
 * \brief One page of the PDF export: its output files and how it will be stored.
 */
class PdfExportPage {
 public:
  enum Kind {
    /** There is no output file yet. */
    MISSING,
    /** 1 bit output. */
    BITONAL,
    /** Split output: the background picture with a 1 bit foreground mask on top. */
    MRC,
    /** Grayscale or colour output, stored as one image. */
    IMAGE,
    /** Posterized colour output (a palette image), stored as one image. */
    PALETTE
  };

  PdfExportPage() = default;

  /**
   * \param mainFile The output file of the page.
   * \param foregroundFile Where the split output puts the foreground layer of the page.
   * \param backgroundFile Where the split output puts the background layer of the page.
   * \param mixedMode Whether the page is set to the "Mixed" colour mode.
   */
  PdfExportPage(const QString& mainFile, const QString& foregroundFile, const QString& backgroundFile, bool mixedMode);

  /**
   * \brief Inspects the files (only their headers) and decides how the page will be stored.
   *
   * Sets kind(), warning(), isWithoutSplitOutput() and the compressions.
   */
  void analyze();

  const QString& mainFile() const { return m_mainFile; }

  const QString& foregroundFile() const { return m_foregroundFile; }

  const QString& backgroundFile() const { return m_backgroundFile; }

  /** Whether the page is set to the "Mixed" colour mode. */
  bool isMixedMode() const { return m_mixedMode; }

  /** The project page whose output this is. A null PageId if the page wasn't made from a project. */
  const PageId& pageId() const { return m_pageId; }

  void setPageId(const PageId& pageId) { m_pageId = pageId; }

  Kind kind() const { return m_kind; }

  /**
   * Whether the output files exist, but no longer match the settings of the page (the hourglass of
   * the Output step), so the PDF would show an older state.
   */
  bool isOutdated() const { return m_outdated; }

  void setOutdated(const bool outdated) { m_outdated = outdated; }

  /** MRC only: whether the background picture is posterized (a palette image). */
  bool hasPalettePicture() const { return m_palettePicture; }

  /** About the files of the page, such as split output files that don't match. Empty if they are fine. */
  const QString& warning() const { return m_warning; }

  /** A mixed page that has no split output files, so it's stored as a single image. */
  bool isWithoutSplitOutput() const { return m_withoutSplitOutput; }

  /**
   * The TIFF compression of the picture, a COMPRESSION_* value of libtiff: of the output file, or
   * of the background layer with split output. 0 for missing pages.
   */
  int pictureCompression() const { return m_pictureCompression; }

  /** MRC only: the TIFF compression of the foreground layer, the text. */
  int textCompression() const { return m_textCompression; }

  /** Whether a TIFF compression loses image quality. Unknown methods count as lossy. */
  static bool isLossyTiffCompression(int compression);

  /** The name of a TIFF compression, as in the TIFF Compression panel of the Output step. */
  static QString tiffCompressionName(int compression);

  /**
   * The project pages of \p pages that the Output step has to process (again), because their
   * output is missing or outdated.
   */
  static std::set<PageId> pagesToOutput(const std::vector<PdfExportPage>& pages);

 private:
  QString m_mainFile;
  QString m_foregroundFile;
  QString m_backgroundFile;
  PageId m_pageId;
  bool m_mixedMode = false;
  Kind m_kind = MISSING;
  bool m_outdated = false;
  bool m_palettePicture = false;
  int m_pictureCompression = 0;
  int m_textCompression = 0;
  bool m_withoutSplitOutput = false;
  QString m_warning;
};


#endif  // SCANTAILOR_CORE_PDFEXPORTPAGE_H_
