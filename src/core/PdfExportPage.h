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
    /** 1 bit output, stored as JBIG2 or CCITT G4. */
    BITONAL,
    /** Split output: JPEG background with a JBIG2 or G4 foreground mask on top. */
    MRC,
    /** Grayscale or colour output, stored as one JPEG image. */
    IMAGE
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

  /** Empty if there is nothing to warn about. */
  const QString& warning() const { return m_warning; }

 private:
  QString m_mainFile;
  QString m_foregroundFile;
  QString m_backgroundFile;
  bool m_mixedMode = false;
  Kind m_kind = MISSING;
  QString m_warning;
};


#endif  // SCANTAILOR_CORE_PDFEXPORTPAGE_H_
