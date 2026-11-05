// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_EXPORTJOB_H_
#define SCANTAILOR_CORE_EXPORTJOB_H_

#include <QObject>
#include <QStringList>
#include <atomic>
#include <vector>

#include "ExportPage.h"
#include "PdfCompression.h"
#include "PdfWriter.h"

struct ProcessingOptions;

struct ExportOptions {
  /** Grayscale and colour pages and the pictures of split pages: NONE, DEFLATE, JPEG or JPEG2000. */
  PdfCompression colorCompression = PdfCompression::JPEG;
  /** 1 to 100. */
  int colorQuality = 85;
  /** The background of split pages is stored at 1 / backgroundScale of the output resolution. */
  int backgroundScale = 2;
  /**
   * Posterized colour pages and pictures of split pages (see PdfImageEncoder::isPalette()):
   * NONE, DEFLATE, JPEG or JPEG2000.
   */
  PdfCompression paletteCompression = PdfCompression::DEFLATE;
  /** 1 to 100. */
  int paletteQuality = 85;
  /** Black and white pages and the text of split pages: NONE, DEFLATE, CCITT_G4 or JBIG2. */
  PdfCompression bitonalCompression = PdfCompression::JBIG2;
  /** Write the PDF as PDF/A-2b. */
  bool pdfA = false;
  /** The number of pages prepared in parallel. */
  int threadCount = 1;
  /** Add an invisible text layer. Requires a build with ENABLE_OCR. */
  bool ocr = false;
  /** The folder with the language files (see OcrLanguages::commonDir()). */
  QString ocrDataDir;
  /** Language codes joined with '+', e.g. "deu+eng". */
  QString ocrLanguages;
  /** One of OcrEngine::PageLayout. */
  int ocrPageLayout = 0;

  /**
   * The compression of the project. The number of threads and the text recognition are
   * left at their defaults; they are up to the caller.
   */
  static ExportOptions fromProject(const ProcessingOptions& options);

  /** The outcome of setUpOcr(). */
  enum class OcrSetup {
    DONE,
    /** No language was given. */
    NO_LANGUAGE,
    /** The languages are in different folders and couldn't be copied into one. */
    NOT_GATHERED
  };

  /**
   * \brief Turns on the text recognition with \p languages and \p pageLayout.
   *
   * The languages must be installed (see OcrLanguages::missing()); what to do about missing
   * ones is up to the caller. Tesseract needs all languages in one folder, so if they are spread
   * over several, they are copied into the user's folder first, which is only needed once.
   * Unless DONE is returned, the options are left unchanged; for NOT_GATHERED, \p error
   * receives the reason.
   */
  OcrSetup setUpOcr(const QStringList& languages, int pageLayout, QString* error);
};

class OcrEngine;


/**
 * \brief Builds a PDF from already existing output files.
 *
 * run() blocks and is meant to be called from a worker thread. Several pages are
 * prepared (loaded and encoded) in parallel, while the PDF is written in page order.
 * The file is written to a temporary file first, so a failed or cancelled export
 * doesn't leave a partial PDF behind or damage an existing one.
 */
class ExportJob : public QObject {
  Q_OBJECT
 public:
  ExportJob(std::vector<ExportPage> pages, const QString& outputFile, const ExportOptions& options);

  void run();

  /** May be called from any thread. */
  void cancel() { m_cancelRequested = true; }

  bool succeeded() const { return m_succeeded; }

  bool wasCancelled() const { return m_cancelled; }

  const QStringList& errors() const { return m_errors; }

  /** The thread limit from the settings (see ThreadBudget). */
  static int defaultThreadCount();

  /**
   * \brief Loads and encodes the files of one page and recognizes its text.
   *
   * \param ocrEngine Used for the text layer if options.ocr is set. Must be initialized.
   * \param cancel If not null, text recognition stops as soon as it becomes true.
   * \return false on failure, in which case \p errors receives the reasons.
   */
  static bool preparePage(const ExportPage& page,
                          const ExportOptions& options,
                          PdfWriter::Page* result,
                          QStringList* errors,
                          OcrEngine* ocrEngine = nullptr,
                          const std::atomic<bool>* cancel = nullptr);

 signals:
  /** Emitted from the thread executing run() after each written page. */
  void progress(int pagesDone, int pagesTotal);

 private:
  std::vector<ExportPage> m_pages;
  QString m_outputFile;
  ExportOptions m_options;
  std::atomic<bool> m_cancelRequested{false};
  bool m_succeeded = false;
  bool m_cancelled = false;
  QStringList m_errors;
};


#endif  // SCANTAILOR_CORE_EXPORTJOB_H_
