// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OcrEngine.h"

#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <tesseract/resultiterator.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <algorithm>
#include <cmath>
#include <mutex>

namespace {
bool cancelRequested(void* cancelFlag, int /*words*/) {
  const auto* flag = static_cast<const std::atomic<bool>*>(cancelFlag);
  return flag && flag->load();
}

/** Several pages are recognized in parallel already, so Tesseract itself shouldn't spawn threads. */
void limitTesseractThreads() {
  static std::once_flag once;
  std::call_once(once, [] {
    if (qEnvironmentVariableIsEmpty("OMP_THREAD_LIMIT")) {
      qputenv("OMP_THREAD_LIMIT", "1");
    }
  });
}
}  // namespace

OcrEngine::OcrEngine() = default;

OcrEngine::~OcrEngine() {
  if (m_api) {
    m_api->End();
  }
}

bool OcrEngine::init(const QString& tessdataDir, const QString& languages, QString* error) {
  limitTesseractThreads();
  m_api = std::make_unique<tesseract::TessBaseAPI>();
  const QByteArray dir = QFile::encodeName(QDir::toNativeSeparators(tessdataDir));
  const QByteArray langs = languages.toUtf8();
  if (m_api->Init(dir.constData(), langs.constData(), tesseract::OEM_LSTM_ONLY) != 0) {
    m_api.reset();
    if (error) {
      *error = QCoreApplication::translate("OcrEngine", "Could not load the language files \"%1\" from %2.")
                   .arg(languages, QDir::toNativeSeparators(tessdataDir));
    }
    return false;
  }
  return true;
}

bool OcrEngine::recognize(const QImage& image,
                          const PageLayout layout,
                          const std::atomic<bool>* cancel,
                          std::vector<OcrWord>* words,
                          QString* error) {
  words->clear();
  if (!m_api) {
    if (error) {
      *error = QCoreApplication::translate("OcrEngine", "Text recognition isn't initialized.");
    }
    return false;
  }

  switch (layout) {
    case SINGLE_COLUMN:
      m_api->SetPageSegMode(tesseract::PSM_SINGLE_COLUMN);
      break;
    case SINGLE_BLOCK:
      m_api->SetPageSegMode(tesseract::PSM_SINGLE_BLOCK);
      break;
    default:
      m_api->SetPageSegMode(tesseract::PSM_AUTO);
      break;
  }

  // Tesseract binarizes the image itself; 1 bit images are passed as gray.
  const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  m_api->SetImage(gray.constBits(), gray.width(), gray.height(), 1, static_cast<int>(gray.bytesPerLine()));
  int dpi = static_cast<int>(std::lround(image.dotsPerMeterY() * 0.0254));
  if (dpi < 10) {
    dpi = 300;
  }
  m_api->SetSourceResolution(dpi);

  tesseract::ETEXT_DESC monitor;
  monitor.cancel = &cancelRequested;
  monitor.cancel_this = const_cast<std::atomic<bool>*>(cancel);
  if (m_api->Recognize(&monitor) != 0) {
    m_api->Clear();
    if (error) {
      *error = (cancel && cancel->load()) ? QCoreApplication::translate("OcrEngine", "Cancelled.")
                                          : QCoreApplication::translate("OcrEngine", "Text recognition failed.");
    }
    return false;
  }

  const std::unique_ptr<tesseract::ResultIterator> it(m_api->GetIterator());
  if (it) {
    QRect lineBox;
    bool haveBaseline = false;
    int bx1 = 0, by1 = 0, bx2 = 0, by2 = 0;
    do {
      if (it->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) {
        int left = 0, top = 0, right = 0, bottom = 0;
        it->BoundingBox(tesseract::RIL_TEXTLINE, &left, &top, &right, &bottom);
        lineBox = QRect(left, top, right - left, bottom - top);
        haveBaseline = it->Baseline(tesseract::RIL_TEXTLINE, &bx1, &by1, &bx2, &by2);
      }
      if (it->Empty(tesseract::RIL_WORD)) {
        continue;
      }
      const std::unique_ptr<char[]> utf8(it->GetUTF8Text(tesseract::RIL_WORD));
      if (!utf8) {
        continue;
      }
      const QString text = QString::fromUtf8(utf8.get()).trimmed();
      if (text.isEmpty()) {
        continue;
      }

      int left = 0, top = 0, right = 0, bottom = 0;
      it->BoundingBox(tesseract::RIL_WORD, &left, &top, &right, &bottom);
      OcrWord word;
      word.text = text;
      word.box = QRect(left, top, right - left, bottom - top);
      // The baseline of the line at the middle of the word.
      int baseline = bottom;
      if (haveBaseline && (bx2 != bx1)) {
        const double x = 0.5 * (left + right);
        baseline = static_cast<int>(std::lround(by1 + (by2 - by1) * (x - bx1) / (bx2 - bx1)));
      } else if (haveBaseline) {
        baseline = by1;
      }
      word.baselineY = std::max(top + 1, std::min(baseline, bottom));
      word.lineTop = lineBox.isValid() ? std::min(lineBox.top(), top) : top;
      word.lastInLine = it->IsAtFinalElement(tesseract::RIL_TEXTLINE, tesseract::RIL_WORD);
      words->push_back(word);
    } while (it->Next(tesseract::RIL_WORD));
  }

  m_api->Clear();
  return true;
}  // OcrEngine::recognize
