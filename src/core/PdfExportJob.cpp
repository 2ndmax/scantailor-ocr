// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportJob.h"

#include <QDir>
#include <QImage>
#include <QSaveFile>
#include <QThread>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

#include "ImageLoadErrors.h"
#include "ImageLoader.h"
#include "OcrEngine.h"
#include "PdfImageEncoder.h"

namespace {
QString describeFailure(const QString& format, const QString& filePath, const QStringList& messages) {
  QString text = format.arg(QDir::toNativeSeparators(filePath));
  if (!messages.isEmpty()) {
    text += ' ' + messages.join(' ');
  }
  return text;
}

QImage loadImage(const QString& filePath, QStringList* errors) {
  ImageLoadErrorCapture capture;
  QImage image = ImageLoader::load(filePath);
  if (image.isNull()) {
    errors->push_back(describeFailure(PdfExportJob::tr("Could not load %1."), filePath, capture.messages()));
  }
  return image;
}

/** Resolution in pixels per point (1/72 inch).  Falls back to 300 dpi if the image has none. */
QSizeF pixelsPerPoint(const QImage& image) {
  auto perPoint = [](const int dotsPerMeter) {
    double dpi = dotsPerMeter * 0.0254;
    if (dpi < 10.0) {
      dpi = 300.0;
    }
    return dpi / 72.0;
  };
  return QSizeF(perPoint(image.dotsPerMeterX()), perPoint(image.dotsPerMeterY()));
}

QRectF toPoints(const QRectF& pixelRect, const QSizeF& perPoint) {
  return QRectF(pixelRect.x() / perPoint.width(), pixelRect.y() / perPoint.height(),
                pixelRect.width() / perPoint.width(), pixelRect.height() / perPoint.height());
}

/** Stores \p image uncompressed or with Deflate, keeping a palette.  1 bit images become masks if \p isMask. */
bool makeLosslessImage(const QImage& image,
                       const QString& filePath,
                       const PdfCompression compression,
                       const bool isMask,
                       const QRectF& rect,
                       PdfWriter::Image* result,
                       QStringList* errors) {
  ImageLoadErrorCapture capture;
  QByteArray samples
      = PdfImageEncoder::rawSamples(image, &result->components, &result->bitsPerComponent, &result->palette);
  if (compression == PdfCompression::DEFLATE) {
    // The predictors help with photos, not with few colours or black and white.
    const bool predictors = result->palette.isEmpty() && (result->bitsPerComponent == 8);
    result->encoding = PdfWriter::Image::Encoding::FLATE;
    result->pngPredictors = predictors;
    result->data = samples.isEmpty()
                       ? QByteArray()
                       : PdfImageEncoder::deflate(samples, image.width(), image.height(), result->components,
                                                  result->bitsPerComponent, predictors);
  } else {
    result->encoding = PdfWriter::Image::Encoding::RAW;
    result->data = std::move(samples);
  }
  result->width = image.width();
  result->height = image.height();
  result->isMask = isMask && (result->bitsPerComponent == 1) && result->palette.isEmpty();
  result->rect = rect;
  if (result->data.isEmpty()) {
    errors->push_back(describeFailure(PdfExportJob::tr("Could not compress %1."), filePath, capture.messages()));
    return false;
  }
  return true;
}

bool makeBitonalImage(const QImage& image,
                      const QString& filePath,
                      const bool isMask,
                      const PdfCompression compression,
                      const QRectF& rect,
                      PdfWriter::Image* result,
                      QStringList* errors) {
  if ((compression == PdfCompression::NONE) || (compression == PdfCompression::DEFLATE)) {
    // The image is converted to 1 bit like for the other methods.
    QImage mono = image;
    if (!PdfImageEncoder::isBitonal(mono)) {
      mono = mono.convertToFormat(QImage::Format_Mono, Qt::ThresholdDither);
    }
    return makeLosslessImage(mono, filePath, compression, isMask, rect, result, errors);
  }
  ImageLoadErrorCapture capture;
  if (compression == PdfCompression::CCITT_G4) {
    result->encoding = PdfWriter::Image::Encoding::CCITT_G4;
    result->data = PdfImageEncoder::encodeG4(image);
  } else {
    result->encoding = PdfWriter::Image::Encoding::JBIG2;
    result->data = PdfImageEncoder::encodeJbig2(image);
  }
  result->width = image.width();
  result->height = image.height();
  result->isMask = isMask;
  result->rect = rect;
  if (result->data.isEmpty()) {
    errors->push_back(describeFailure(PdfExportJob::tr("Could not compress %1."), filePath, capture.messages()));
    return false;
  }
  return true;
}

bool makeJpegImage(const QImage& image,
                   const QString& filePath,
                   const int quality,
                   const QRectF& rect,
                   PdfWriter::Image* result,
                   QStringList* errors) {
  ImageLoadErrorCapture capture;
  result->encoding = PdfWriter::Image::Encoding::JPEG;
  result->data = PdfImageEncoder::encodeJpeg(image, quality, &result->components);
  result->width = image.width();
  result->height = image.height();
  result->rect = rect;
  if (result->data.isEmpty()) {
    errors->push_back(describeFailure(PdfExportJob::tr("Could not compress %1."), filePath, capture.messages()));
    return false;
  }
  return true;
}

/** Grayscale, colour and palette images. */
bool makeColorImage(const QImage& image,
                    const QString& filePath,
                    const PdfCompression compression,
                    const int quality,
                    const QRectF& rect,
                    PdfWriter::Image* result,
                    QStringList* errors) {
  if ((compression == PdfCompression::NONE) || (compression == PdfCompression::DEFLATE)) {
    return makeLosslessImage(image, filePath, compression, false, rect, result, errors);
  }
  // A palette image is converted to full colour for JPEG.
  return makeJpegImage(image, filePath, quality, rect, result, errors);
}

/** Scales a palette image without smoothing, so no colours are added. */
QImage scalePaletteImage(const QImage& image, const int width, const int height) {
  QImage scaled(width, height, QImage::Format_Indexed8);
  scaled.setColorTable(image.colorTable());
  for (int y = 0; y < height; ++y) {
    const uchar* src = image.constScanLine(static_cast<int>(static_cast<qint64>(y) * image.height() / height));
    uchar* dst = scaled.scanLine(y);
    for (int x = 0; x < width; ++x) {
      dst[x] = src[static_cast<qint64>(x) * image.width() / width];
    }
  }
  return scaled;
}

/** Recognizes the text of \p image, if requested, and adds it to the page as invisible text. */
bool addTextLayer(const QImage& image,
                  const QString& filePath,
                  const QSizeF& perPoint,
                  const PdfExportOptions& options,
                  OcrEngine* ocrEngine,
                  const std::atomic<bool>* cancel,
                  PdfWriter::Page* result,
                  QStringList* errors) {
  if (!options.ocr) {
    return true;
  }
#ifdef ENABLE_OCR
  if (!ocrEngine) {
    errors->push_back(PdfExportJob::tr("Text recognition isn't initialized."));
    return false;
  }
  std::vector<OcrWord> words;
  QString error;
  if (!ocrEngine->recognize(image, static_cast<OcrEngine::PageLayout>(options.ocrPageLayout), cancel, &words, &error)) {
    errors->push_back(
        describeFailure(PdfExportJob::tr("Text recognition failed for %1."), filePath, QStringList(error)));
    return false;
  }
  for (const OcrWord& ocrWord : words) {
    PdfWriter::Word word;
    // The space makes viewers separate the words when copying text.
    word.text = ocrWord.lastInLine ? ocrWord.text : ocrWord.text + ' ';
    word.origin = QPointF(ocrWord.box.left() / perPoint.width(), ocrWord.baselineY / perPoint.height());
    word.width = ocrWord.box.width() / perPoint.width();
    // The selection then reaches from the baseline to the top of the line.
    const int heightPx
        = (ocrWord.baselineY > ocrWord.lineTop) ? (ocrWord.baselineY - ocrWord.lineTop) : ocrWord.box.height();
    word.fontSize = std::max(1, heightPx) / perPoint.height();
    result->words.push_back(std::move(word));
  }
  return true;
#else
  Q_UNUSED(image);
  Q_UNUSED(filePath);
  Q_UNUSED(perPoint);
  Q_UNUSED(ocrEngine);
  Q_UNUSED(cancel);
  Q_UNUSED(result);
  errors->push_back(PdfExportJob::tr("This version of the program was built without text recognition."));
  return false;
#endif
}
}  // namespace

PdfExportJob::PdfExportJob(std::vector<PdfExportPage> pages, const QString& outputFile, const PdfExportOptions& options)
    : m_pages(std::move(pages)), m_outputFile(outputFile), m_options(options) {}

int PdfExportJob::defaultThreadCount() {
  return std::clamp(QThread::idealThreadCount() - 2, 1, 16);
}

bool PdfExportJob::preparePage(const PdfExportPage& page,
                               const PdfExportOptions& options,
                               PdfWriter::Page* result,
                               QStringList* errors,
                               OcrEngine* ocrEngine,
                               const std::atomic<bool>* cancel) {
  result->images.clear();
  result->words.clear();

  switch (page.kind()) {
    case PdfExportPage::MISSING: {
      errors->push_back(describeFailure(tr("The output file %1 doesn't exist."), page.mainFile(), QStringList()));
      return false;
    }
    case PdfExportPage::BITONAL:
    case PdfExportPage::PALETTE:
    case PdfExportPage::IMAGE: {
      const QImage image = loadImage(page.mainFile(), errors);
      if (image.isNull()) {
        return false;
      }
      const QSizeF perPoint = pixelsPerPoint(image);
      result->size = QSizeF(image.width() / perPoint.width(), image.height() / perPoint.height());
      const QRectF rect(QPointF(0, 0), result->size);

      PdfWriter::Image pdfImage;
      // The kind was determined from the file header; decide by the actual image to be safe.
      bool ok;
      if (PdfImageEncoder::isBitonal(image)) {
        ok = makeBitonalImage(image, page.mainFile(), false, options.bitonalCompression, rect, &pdfImage, errors);
      } else if (PdfImageEncoder::isPalette(image)) {
        ok = makeColorImage(image, page.mainFile(), options.paletteCompression, options.paletteQuality, rect, &pdfImage,
                            errors);
      } else {
        ok = makeColorImage(image, page.mainFile(), options.colorCompression, options.colorQuality, rect, &pdfImage,
                            errors);
      }
      if (!ok) {
        return false;
      }
      result->images.push_back(std::move(pdfImage));
      return addTextLayer(image, page.mainFile(), perPoint, options, ocrEngine, cancel, result, errors);
    }
    case PdfExportPage::MRC: {
      const QImage foreground = loadImage(page.foregroundFile(), errors);
      if (foreground.isNull()) {
        return false;
      }
      const QSizeF perPoint = pixelsPerPoint(foreground);
      result->size = QSizeF(foreground.width() / perPoint.width(), foreground.height() / perPoint.height());

      {
        QImage background = loadImage(page.backgroundFile(), errors);
        if (background.isNull()) {
          return false;
        }
        // Only the part with pictures is stored, the rest of the background is white.
        const QRect content = PdfImageEncoder::contentRect(background);
        if (!content.isEmpty()) {
          QImage part = background.copy(content);
          background = QImage();
          // Posterized pictures keep their palette.
          const bool palette = PdfImageEncoder::isPalette(part);
          if (!palette) {
            part = part.convertToFormat(part.isGrayscale() ? QImage::Format_Grayscale8 : QImage::Format_RGB32);
          }
          const int scale = std::max(1, options.backgroundScale);
          if (scale > 1) {
            const int width = std::max(1, static_cast<int>(std::ceil(part.width() / static_cast<double>(scale))));
            const int height = std::max(1, static_cast<int>(std::ceil(part.height() / static_cast<double>(scale))));
            part = palette ? scalePaletteImage(part, width, height)
                           : part.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          }
          PdfWriter::Image pdfImage;
          const PdfCompression compression = palette ? options.paletteCompression : options.colorCompression;
          const int quality = palette ? options.paletteQuality : options.colorQuality;
          if (!makeColorImage(part, page.backgroundFile(), compression, quality, toPoints(QRectF(content), perPoint),
                              &pdfImage, errors)) {
            return false;
          }
          result->images.push_back(std::move(pdfImage));
        }
      }

      PdfWriter::Image mask;
      if (!makeBitonalImage(foreground, page.foregroundFile(), true, options.bitonalCompression,
                            QRectF(QPointF(0, 0), result->size), &mask, errors)) {
        return false;
      }
      result->images.push_back(std::move(mask));
      // The foreground holds only the text, without the pictures, which helps recognition.
      return addTextLayer(foreground, page.foregroundFile(), perPoint, options, ocrEngine, cancel, result, errors);
    }
  }
  return false;
}  // PdfExportJob::preparePage

void PdfExportJob::run() {
  m_succeeded = false;
  m_cancelled = false;
  m_errors.clear();

  const int pageCount = static_cast<int>(m_pages.size());
  if (pageCount == 0) {
    m_errors.push_back(tr("No pages are selected."));
    return;
  }

  QSaveFile file(m_outputFile);
  if (!file.open(QIODevice::WriteOnly)) {
    m_errors.push_back(describeFailure(tr("Could not create %1."), m_outputFile, QStringList(file.errorString())));
    return;
  }

  PdfWriter writer(file);
  writer.setProducer(QStringLiteral("ScanTailor OCR"));
  bool ok = writer.begin();

  struct Prepared {
    bool ok = false;
    PdfWriter::Page page;
    QStringList errors;

    qint64 bytes() const {
      qint64 sum = 0;
      for (const PdfWriter::Image& image : page.images) {
        sum += image.data.size();
      }
      return sum;
    }
  };

  // Workers prepare pages in parallel, this thread writes them in order.
  // To limit memory usage, workers don't run too far ahead of the writer,
  // neither in pages nor in bytes: uncompressed pages can be dozens of MB.
  std::mutex mutex;
  std::condition_variable condition;
  std::map<int, Prepared> prepared;
  qint64 preparedBytes = 0;
  const qint64 maxPreparedBytes = qint64(512) * 1024 * 1024;
  int nextToPrepare = 0;
  int nextToWrite = 0;
  bool stop = false;
  const int threadCount = std::clamp(m_options.threadCount, 1, pageCount);
  const int maxAhead = threadCount * 2;

  auto worker = [&]() {
  // Each thread has its own recognizer, created when it prepares its first page.
#ifdef ENABLE_OCR
    std::unique_ptr<OcrEngine> ocrEngine;
#endif
    OcrEngine* engine = nullptr;
    QString ocrInitError;

    while (true) {
      int index;
      {
        std::unique_lock<std::mutex> lock(mutex);
        // The page the writer waits for is always being prepared already, so waiting
        // for memory to be freed can't block the writer.
        condition.wait(lock, [&] {
          return stop
                 || ((nextToPrepare < std::min(pageCount, nextToWrite + maxAhead))
                     && ((preparedBytes < maxPreparedBytes) || prepared.empty()));
        });
        if (stop || (nextToPrepare >= pageCount)) {
          return;
        }
        index = nextToPrepare++;
      }

      Prepared item;
      if (!m_cancelRequested) {
        try {
#ifdef ENABLE_OCR
          if (m_options.ocr && !ocrEngine && ocrInitError.isEmpty()) {
            auto newEngine = std::make_unique<OcrEngine>();
            if (newEngine->init(m_options.ocrDataDir, m_options.ocrLanguages, &ocrInitError)) {
              ocrEngine = std::move(newEngine);
              engine = ocrEngine.get();
            } else if (ocrInitError.isEmpty()) {
              ocrInitError = tr("Text recognition could not be started.");
            }
          }
#endif
          if (!ocrInitError.isEmpty()) {
            item.ok = false;
            item.errors.push_back(ocrInitError);
          } else {
            item.ok = preparePage(m_pages[index], m_options, &item.page, &item.errors, engine, &m_cancelRequested);
          }
        } catch (const std::bad_alloc&) {
          item.ok = false;
          item.errors.push_back(tr("Out of memory."));
        } catch (const std::exception& e) {
          item.ok = false;
          item.errors.push_back(QString::fromLocal8Bit(e.what()));
        } catch (...) {
          // An exception escaping a std::thread would terminate the program.
          item.ok = false;
          item.errors.push_back(tr("Unknown error."));
        }
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        preparedBytes += item.bytes();
        prepared[index] = std::move(item);
      }
      condition.notify_all();
    }
  };

  std::vector<std::thread> threads;
  threads.reserve(threadCount);
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back(worker);
  }

  while (ok && (nextToWrite < pageCount)) {
    Prepared item;
    {
      std::unique_lock<std::mutex> lock(mutex);
      // Wake up regularly to notice a cancellation request.
      while (!m_cancelRequested && (prepared.find(nextToWrite) == prepared.end())) {
        condition.wait_for(lock, std::chrono::milliseconds(100));
      }
      if (m_cancelRequested) {
        m_cancelled = true;
        break;
      }
      auto it = prepared.find(nextToWrite);
      item = std::move(it->second);
      prepared.erase(it);
      preparedBytes -= item.bytes();
      ++nextToWrite;
    }
    condition.notify_all();

    if (!item.ok) {
      m_errors.append(item.errors);
      ok = false;
      break;
    }
    if (!writer.addPage(item.page)) {
      ok = false;
      break;
    }
    emit progress(nextToWrite, pageCount);
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread& thread : threads) {
    thread.join();
  }

  if (ok && !m_cancelled) {
    ok = writer.finish();
  }
  if (ok && !m_cancelled) {
    if (file.commit()) {
      m_succeeded = true;
      return;
    }
    m_errors.push_back(describeFailure(tr("Could not write %1."), m_outputFile, QStringList(file.errorString())));
    return;
  }

  file.cancelWriting();
  if (!m_cancelled && m_errors.isEmpty()) {
    m_errors.push_back(describeFailure(tr("Could not write %1."), m_outputFile, QStringList(file.errorString())));
  }
}  // PdfExportJob::run
