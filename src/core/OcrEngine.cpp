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

namespace {
bool cancelRequested(void* cancelFlag, int /*words*/) {
  const auto* flag = static_cast<const std::atomic<bool>*>(cancelFlag);
  return flag && flag->load();
}

void assignError(QString* error, const QString& message) {
  if (error) {
    *error = message;
  }
}

tesseract::PageSegMode toPageSegMode(const OcrEngine::PageLayout layout) {
  switch (layout) {
    case OcrEngine::SINGLE_COLUMN:
      return tesseract::PSM_SINGLE_COLUMN;
    case OcrEngine::SINGLE_BLOCK:
      return tesseract::PSM_SINGLE_BLOCK;
    default:
      return tesseract::PSM_AUTO;
  }
}

int imageDpi(const QImage& image) {
  const int dpi = static_cast<int>(std::lround(image.dotsPerMeterY() * 0.0254));
  return (dpi < 10) ? 300 : dpi;
}

// Taking ownership of what Tesseract allocated; null means it failed.
bool takeTesseractText(char* text, QByteArray* target) {
  const std::unique_ptr<char[]> owned(text);
  if (owned) {
    *target = QByteArray(owned.get());
  }
  return owned != nullptr;
}

bool extractRequestedTexts(tesseract::TessBaseAPI* api, const OcrRequest& request, OcrPageTexts* texts) {
  bool delivered = true;
  if (request.text) {
    delivered = takeTesseractText(api->GetUTF8Text(), &texts->text) && delivered;
  }
  if (request.hocr) {
    // Tesseract puts the name into the title of the page and escapes only <>&"', so the characters XML doesn't
    // allow at all are replaced here.
    // REVIEW-NOTE (2026-11): no regression test -> the tests have no language data, and Tesseract can't
    // recognize without one. xmlCharacters() itself is tested.
    api->SetInputName(OcrTextOutputs::xmlCharacters(request.imageName).toUtf8().constData());
    // hOCR counts from 0 and adds 1 itself, so the IDs are unique in the file of all pages.
    delivered = takeTesseractText(api->GetHOCRText(request.position - 1), &texts->hocr) && delivered;
  }
  if (request.alto) {
    // ALTO takes the number as it is and leaves it out of the IDs if it is 0, so this counts from 1,
    // like the number in the name of the file.
    delivered = takeTesseractText(api->GetAltoText(request.position), &texts->alto) && delivered;
  }
  return delivered;
}

struct LineMetrics {
  QRect box;
  bool haveBaseline = false;
  int bx1 = 0;
  int by1 = 0;
  int bx2 = 0;
  int by2 = 0;
};

void updateLineMetrics(tesseract::ResultIterator* it, LineMetrics* line) {
  if (!it->IsAtBeginningOf(tesseract::RIL_TEXTLINE)) {
    return;
  }
  int left = 0, top = 0, right = 0, bottom = 0;
  if (it->BoundingBox(tesseract::RIL_TEXTLINE, &left, &top, &right, &bottom)) {
    line->box = QRect(left, top, right - left, bottom - top);
    line->haveBaseline = it->Baseline(tesseract::RIL_TEXTLINE, &line->bx1, &line->by1, &line->bx2, &line->by2);
  } else {
    line->box = QRect();
    line->haveBaseline = false;
  }
}

int computeWordBaselineY(const LineMetrics& line, const int left, const int top, const int right, const int bottom) {
  int baseline = bottom;
  if (line.haveBaseline && (line.bx2 != line.bx1)) {
    const double x = 0.5 * (left + right);
    baseline = static_cast<int>(std::lround(line.by1 + (line.by2 - line.by1) * (x - line.bx1) / (line.bx2 - line.bx1)));
  } else if (line.haveBaseline) {
    baseline = line.by1;
  }
  return std::max(top + 1, std::min(baseline, bottom));
}

bool readCurrentWord(tesseract::ResultIterator* it, const LineMetrics& line, OcrWord* word) {
  if (it->Empty(tesseract::RIL_WORD)) {
    return false;
  }
  const std::unique_ptr<char[]> utf8(it->GetUTF8Text(tesseract::RIL_WORD));
  if (!utf8) {
    return false;
  }
  const QString text = QString::fromUtf8(utf8.get()).trimmed();
  if (text.isEmpty()) {
    return false;
  }

  int left = 0, top = 0, right = 0, bottom = 0;
  if (!it->BoundingBox(tesseract::RIL_WORD, &left, &top, &right, &bottom)) {
    return false;
  }
  word->text = text;
  word->box = QRect(left, top, right - left, bottom - top);
  // The baseline of the line at the middle of the word.
  word->baselineY = computeWordBaselineY(line, left, top, right, bottom);
  word->lineTop = line.box.isValid() ? std::min(line.box.top(), top) : top;
  word->lastInLine = it->IsAtFinalElement(tesseract::RIL_TEXTLINE, tesseract::RIL_WORD);
  return true;
}

void extractWords(tesseract::TessBaseAPI* api, std::vector<OcrWord>* words) {
  const std::unique_ptr<tesseract::ResultIterator> it(api->GetIterator());
  if (!it) {
    return;
  }
  LineMetrics line;
  do {
    updateLineMetrics(it.get(), &line);
    OcrWord word;
    if (readCurrentWord(it.get(), line, &word)) {
      words->push_back(std::move(word));
    }
  } while (it->Next(tesseract::RIL_WORD));
}
}  // namespace

void OcrEngine::limitThreads() {
  // REVIEW-NOTE (2026-11): libgomp reads OMP_THREAD_LIMIT when the library is loaded, before main() -> on Linux
  // the limit has no effect and Tesseract's OpenMP threads run beside the export's. Not fixed because it only costs
  // speed and setting it in time would need the program to restart itself.
  // REVIEW-NOTE (2026-11): no regression test -> the race with threads reading the environment can't be
  // provoked reliably. Moved here from the first recognition, which ran on a worker thread.
  if (qEnvironmentVariableIsEmpty("OMP_THREAD_LIMIT")) {
    qputenv("OMP_THREAD_LIMIT", "1");
  }
}

OcrEngine::OcrEngine() = default;

OcrEngine::~OcrEngine() {
  if (m_api) {
    m_api->End();
  }
}

bool OcrEngine::init(const QString& tessdataDir, const QString& languages, QString* error) {
  m_api = std::make_unique<tesseract::TessBaseAPI>();
  // In UTF-8, for readDataFile(): Tesseract only joins the folder and the file names.
  const QByteArray dir = QDir::toNativeSeparators(tessdataDir).toUtf8();
  const QByteArray langs = languages.toUtf8();
  // data_size 0: the first argument is the folder, not the content of a language file.
  if (m_api->Init(dir.constData(), 0, langs.constData(), tesseract::OEM_LSTM_ONLY, nullptr, 0, nullptr, nullptr, false,
                  &OcrEngine::readDataFile)
      != 0) {
    m_api.reset();
    if (error) {
      *error = QCoreApplication::translate("OcrEngine", "Could not load the language files \"%1\" from %2.")
                   .arg(languages, QDir::toNativeSeparators(tessdataDir));
    }
    return false;
  }
  return true;
}

bool OcrEngine::readDataFile(const char* utf8FileName, std::vector<char>* data) {
  QFile file(QString::fromUtf8(utf8FileName));
  if (!file.open(QIODevice::ReadOnly)) {
    return false;
  }
  // Read straight into the buffer: the "best" models are large.
  // REVIEW-NOTE (2026-11): a language file about as large as the memory -> bad_alloc out of Tesseract's
  // callback. Not fixed because no limit fits all models and the file is in the tessdata folder the
  // user chose.
  data->resize(static_cast<size_t>(file.size()));
  return file.read(data->data(), file.size()) == file.size();
}

QString OcrEngine::tesseractVersion() {
  return QString::fromUtf8(tesseract::TessBaseAPI::Version());
}

bool OcrEngine::recognize(const QImage& image,
                          const PageLayout layout,
                          const OcrRequest& request,
                          const std::atomic<bool>* cancel,
                          OcrPageResult* result,
                          QString* error) {
  *result = OcrPageResult();
  if (!m_api) {
    assignError(error, QCoreApplication::translate("OcrEngine", "Text recognition isn't initialized."));
    return false;
  }
  if (image.isNull() || image.size().isEmpty()) {
    assignError(error, QCoreApplication::translate("OcrEngine", "Text recognition failed."));
    return false;
  }

  m_api->SetPageSegMode(toPageSegMode(layout));

  // Tesseract binarizes the image itself; 1 bit images are passed as gray.
  const QImage gray = image.convertToFormat(QImage::Format_Grayscale8);
  if (gray.isNull() || (gray.constBits() == nullptr)) {
    assignError(error, QCoreApplication::translate("OcrEngine", "Text recognition failed."));
    return false;
  }
  m_api->SetImage(gray.constBits(), gray.width(), gray.height(), 1, static_cast<int>(gray.bytesPerLine()));
  m_api->SetSourceResolution(imageDpi(image));

  tesseract::ETEXT_DESC monitor;
  monitor.cancel = &cancelRequested;
  monitor.cancel_this = const_cast<std::atomic<bool>*>(cancel);
  if (m_api->Recognize(&monitor) != 0) {
    m_api->Clear();
    assignError(error, (cancel && cancel->load())
                           ? QCoreApplication::translate("OcrEngine", "Canceled.")
                           : QCoreApplication::translate("OcrEngine", "Text recognition failed."));
    return false;
  }

  // These calls only read the result of Recognize(), so they must come before Clear().
  if (!extractRequestedTexts(m_api.get(), request, &result->texts)) {
    m_api->Clear();
    assignError(error, QCoreApplication::translate("OcrEngine", "Text recognition failed."));
    return false;
  }

  if (request.words) {
    extractWords(m_api.get(), &result->words);
  }

  m_api->Clear();
  return true;
}  // OcrEngine::recognize
