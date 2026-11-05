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
#include "ProcessingOptions.h"

struct OcrPageTexts;

/** Where the formats of an export go. */
struct ExportPaths {
  QString pdf;
  QString txt;
  QString hocr;
  QString altoDir;

  /** The one rule for the names, e.g. "D:/Books/Book" gives Book.pdf, Book.txt, Book.hocr and Book_alto. */
  static ExportPaths fromBase(const QString& base);

  /** For callers that are given a PDF file: the other formats go beside it, under the same name. */
  static ExportPaths forPdfFile(const QString& pdfFile);
};

struct ExportOptions {
  /** Which files are created. */
  ProcessingOptions::Formats formats;
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
  /** Whether the PDF gets the recognized text as an invisible layer. Takes effect with ocr. */
  bool textLayer = true;
  /** The text recognition is set up (see setUpOcr()). Requires a build with ENABLE_OCR. */
  bool ocr = false;
  /** The folder with the language files (see OcrLanguages::commonDir()). */
  QString ocrDataDir;
  /** Language codes joined with '+', e.g. "deu+eng". */
  QString ocrLanguages;
  /** One of OcrEngine::PageLayout. */
  int ocrPageLayout = 0;

  /**
   * The formats and the compression of the project. The number of threads and setting up the
   * text recognition are up to the caller.
   */
  static ExportOptions fromProject(const ProcessingOptions& options);

  /** Whether Tesseract runs: it is set up, and one of the chosen formats needs the text. */
  bool runsTextRecognition() const { return ocr && formats.needsTextRecognition(textLayer); }

  /** Whether the words of the pages are needed for the text layer of the PDF. */
  bool addsTextLayer() const { return ocr && formats.pdf && textLayer; }

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
 * \brief Builds the chosen formats from already existing output files.
 *
 * run() blocks and is meant to be called from a worker thread. Several pages are
 * prepared (loaded, encoded and recognized) in parallel, while the files are written in page order.
 * They are written to temporary files first, so a failed or cancelled export
 * doesn't leave partial files behind or damage existing ones.
 */
class ExportJob : public QObject {
  Q_OBJECT
 public:
  ExportJob(std::vector<ExportPage> pages, const ExportPaths& paths, const ExportOptions& options);

  void run();

  /** May be called from any thread. */
  void cancel() { m_cancelRequested = true; }

  bool succeeded() const { return m_succeeded; }

  bool wasCancelled() const { return m_cancelled; }

  const QStringList& errors() const { return m_errors; }

  /** The thread limit from the settings (see ThreadBudget). */
  static int defaultThreadCount();

  /**
   * \brief Loads and encodes the files of one page, as far as the formats need it, and recognizes its text.
   *
   * \param result Stays without images if no PDF is created.
   * \param ocrEngine Used if options.runsTextRecognition(). Must be initialized.
   * \param cancel If not null, text recognition stops as soon as it becomes true.
   * \param position The place of the page in the export, from 1. Named in the hOCR and ALTO text.
   * \param texts If not null, receives the text of the page in the text formats.
   * \return false on failure, in which case \p errors receives the reasons.
   */
  static bool preparePage(const ExportPage& page,
                          const ExportOptions& options,
                          PdfWriter::Page* result,
                          QStringList* errors,
                          OcrEngine* ocrEngine = nullptr,
                          const std::atomic<bool>* cancel = nullptr,
                          int position = 1,
                          OcrPageTexts* texts = nullptr);

 signals:
  /** Emitted from the thread executing run() after each written page. */
  void progress(int pagesDone, int pagesTotal);

 private:
  std::vector<ExportPage> m_pages;
  ExportPaths m_paths;
  ExportOptions m_options;
  std::atomic<bool> m_cancelRequested{false};
  bool m_succeeded = false;
  bool m_cancelled = false;
  QStringList m_errors;
};


#endif  // SCANTAILOR_CORE_EXPORTJOB_H_
