// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFEXPORTPAGE_H_
#define SCANTAILOR_CORE_PDFEXPORTPAGE_H_

#include <QString>

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
   * Sets kind() and warning().
   */
  void analyze();

  const QString& mainFile() const { return m_mainFile; }

  const QString& foregroundFile() const { return m_foregroundFile; }

  const QString& backgroundFile() const { return m_backgroundFile; }

  Kind kind() const { return m_kind; }

  /** MRC only: whether the background picture is posterized (a palette image). */
  bool hasPalettePicture() const { return m_palettePicture; }

  /** Empty if there is nothing to warn about. */
  const QString& warning() const { return m_warning; }

 private:
  QString m_mainFile;
  QString m_foregroundFile;
  QString m_backgroundFile;
  bool m_mixedMode = false;
  Kind m_kind = MISSING;
  bool m_palettePicture = false;
  QString m_warning;
};


#endif  // SCANTAILOR_CORE_PDFEXPORTPAGE_H_
