// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OCRENGINE_H_
#define SCANTAILOR_CORE_OCRENGINE_H_

#include <QRect>
#include <QString>
#include <atomic>
#include <memory>
#include <vector>

#include "NonCopyable.h"

class QImage;

namespace tesseract {
class TessBaseAPI;
}

/** A recognized word, in pixels of the recognized image. */
struct OcrWord {
  QString text;
  QRect box;
  /** The y coordinate of the baseline of the line, at the word. */
  int baselineY = 0;
  /** The top of the line the word belongs to. */
  int lineTop = 0;
  bool lastInLine = false;
};


/**
 * \brief Text recognition with Tesseract.
 *
 * An instance must only be used by one thread at a time.  For parallel recognition,
 * each thread uses its own instance.  Only available if built with ENABLE_OCR.
 */
class OcrEngine {
  DECLARE_NON_COPYABLE(OcrEngine)

 public:
  /** Corresponds to Tesseract's page segmentation modes 3, 4 and 6. */
  enum PageLayout { AUTOMATIC_LAYOUT = 0, SINGLE_COLUMN = 1, SINGLE_BLOCK = 2 };

  OcrEngine();

  ~OcrEngine();

  /**
   * \brief Loads the language files.  Takes a moment with the "best" models.
   *
   * \param tessdataDir The folder holding the *.traineddata files.
   * \param languages Language codes joined with '+', e.g. "deu+eng".
   */
  bool init(const QString& tessdataDir, const QString& languages, QString* error);

  /**
   * \brief Recognizes the text of an image.
   *
   * The resolution of the image (dots per meter) is passed on to Tesseract.
   *
   * \param cancel If not null, recognition stops as soon as it becomes true.
   */
  bool recognize(const QImage& image,
                 PageLayout layout,
                 const std::atomic<bool>* cancel,
                 std::vector<OcrWord>* words,
                 QString* error);

 private:
  std::unique_ptr<tesseract::TessBaseAPI> m_api;
};


#endif  // SCANTAILOR_CORE_OCRENGINE_H_
