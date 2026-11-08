// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ExportJob.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
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
#include "OcrLanguages.h"
#include "OcrTextOutputs.h"
#include "PdfImageEncoder.h"
#include "ProcessingOptions.h"
#include "ThreadBudget.h"

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
    errors->push_back(describeFailure(ExportJob::tr("Could not load %1."), filePath, capture.messages()));
  }
  return image;
}

/** Resolution in pixels per point (1/72 inch). Falls back to 300 dpi if the image has none. */
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

/** Sets the size and place of the compressed image. Reports an error if the compression produced no data. */
bool finishImage(const QImage& image,
                 const QString& filePath,
                 const QRectF& rect,
                 const ImageLoadErrorCapture& capture,
                 PdfWriter::Image* result,
                 QStringList* errors) {
  result->width = image.width();
  result->height = image.height();
  result->rect = rect;
  if (result->data.isEmpty()) {
    errors->push_back(describeFailure(ExportJob::tr("Could not compress %1."), filePath, capture.messages()));
    return false;
  }
  return true;
}

/** Stores \p image uncompressed or with Deflate, keeping a palette. 1 bit images become masks if \p isMask. */
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
  result->isMask = isMask && (result->bitsPerComponent == 1) && result->palette.isEmpty();
  return finishImage(image, filePath, rect, capture, result, errors);
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
  result->isMask = isMask;
  return finishImage(image, filePath, rect, capture, result, errors);
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
  return finishImage(image, filePath, rect, capture, result, errors);
}

bool makeJpeg2000Image(const QImage& image,
                       const QString& filePath,
                       const int quality,
                       const QRectF& rect,
                       PdfWriter::Image* result,
                       QStringList* errors) {
  ImageLoadErrorCapture capture;
  result->encoding = PdfWriter::Image::Encoding::JPX;
  result->data = PdfImageEncoder::encodeJpeg2000(image, quality, &result->components);
  return finishImage(image, filePath, rect, capture, result, errors);
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
  // A palette image is converted to full colour for JPEG and JPEG 2000.
  if (compression == PdfCompression::JPEG2000) {
    return makeJpeg2000Image(image, filePath, quality, rect, result, errors);
  }
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

/**
 * Recognizes the text of \p image, if a format needs it: the words go into the page as invisible
 * text, the texts of the text formats into \p texts. \p page names the image, also if \p image is
 * the foreground of split output, which has the same size and name as the output file.
 */
bool recognizeText(const QImage& image,
                   const QString& filePath,
                   const ExportPage& page,
                   const int position,
                   const QSizeF& perPoint,
                   const ExportOptions& options,
                   OcrEngine* ocrEngine,
                   const std::atomic<bool>* cancel,
                   PdfWriter::Page* result,
                   OcrPageTexts* texts,
                   QStringList* errors) {
  if (!options.runsTextRecognition()) {
    return true;
  }
  if (!ocrEngine) {
    errors->push_back(ExportJob::tr("Text recognition isn't initialized."));
    return false;
  }
  OcrRequest request;
  request.words = options.addsTextLayer();
  request.text = options.formats.txt;
  request.hocr = options.formats.hocr;
  request.alto = options.formats.alto;
  request.imageName = QFileInfo(page.mainFile()).fileName();
  request.position = position;
  OcrPageResult recognized;
  QString error;
  if (!ocrEngine->recognize(image, static_cast<OcrEngine::PageLayout>(options.ocrPageLayout), request, cancel,
                            &recognized, &error)) {
    errors->push_back(describeFailure(ExportJob::tr("Text recognition failed for %1."), filePath, QStringList(error)));
    return false;
  }
  if (texts) {
    *texts = std::move(recognized.texts);
  }
  for (const OcrWord& ocrWord : recognized.words) {
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
}

/** The message for the exception being handled, as the user sees it. Call only in a catch block. */
QString currentExceptionMessage() {
  try {
    throw;
  } catch (const std::bad_alloc&) {
    return ExportJob::tr("Out of memory.");
  } catch (const std::exception& e) {
    return QString::fromLocal8Bit(e.what());
  } catch (...) {
    return ExportJob::tr("Unknown error.");
  }
}
}  // namespace

ExportPaths ExportPaths::fromBase(const QString& base) {
  ExportPaths paths;
  paths.pdf = base + QStringLiteral(".pdf");
  paths.txt = base + QStringLiteral(".txt");
  paths.hocr = base + QStringLiteral(".hocr");
  paths.altoDir = base + QStringLiteral("_alto");
  return paths;
}

QString ExportPaths::withoutFormatExtension(const QString& base) {
  for (const QLatin1String extension :
       {QLatin1String(".pdf"), QLatin1String(".txt"), QLatin1String(".hocr"), QLatin1String(".xml")}) {
    if (base.endsWith(extension, Qt::CaseInsensitive)) {
      return base.chopped(extension.size());
    }
  }
  return base;
}

QString ExportPaths::nameFromInput(const QString& typed) {
  return withoutFormatExtension(typed.trimmed());
}

ExportPaths ExportPaths::forPdfFile(const QString& pdfFile) {
  const QString extension = QStringLiteral(".pdf");
  ExportPaths paths
      = fromBase(pdfFile.endsWith(extension, Qt::CaseInsensitive) ? pdfFile.chopped(extension.size()) : pdfFile);
  paths.pdf = pdfFile;
  return paths;
}

ExportOptions ExportOptions::fromProject(const ProcessingOptions& options) {
  ExportOptions result;
  result.formats = options.formats;
  result.pdf = options.pdf;
  return result;
}

ExportOptions::OcrSetup ExportOptions::setUpOcr(const QStringList& languages, const int pageLayout, QString* error) {
  if (languages.isEmpty()) {
    return OcrSetup::NO_LANGUAGE;
  }
  QString dataDir = OcrLanguages::commonDir(languages);
  if (dataDir.isEmpty()) {
    dataDir = OcrLanguages::gatherInUserDir(languages, error);
    if (dataDir.isEmpty()) {
      return OcrSetup::NOT_GATHERED;
    }
  }
  ocr = true;
  ocrDataDir = dataDir;
  ocrLanguages = languages.join('+');
  ocrPageLayout = pageLayout;
  return OcrSetup::DONE;
}

ExportJob::ExportJob(std::vector<ExportPage> pages, const ExportPaths& paths, const ExportOptions& options)
    : m_pages(std::move(pages)), m_paths(paths), m_options(options) {}

int ExportJob::defaultThreadCount() {
  return ThreadBudget::maxThreads();
}

bool ExportJob::preparePage(const ExportPage& page,
                            const ExportOptions& options,
                            PdfWriter::Page* result,
                            QStringList* errors,
                            OcrEngine* ocrEngine,
                            const std::atomic<bool>* cancel,
                            const int position,
                            OcrPageTexts* texts) {
  result->images.clear();
  result->words.clear();

  switch (page.kind()) {
    case ExportPage::MISSING: {
      errors->push_back(describeFailure(tr("The output file %1 doesn't exist."), page.mainFile(), QStringList()));
      return false;
    }
    case ExportPage::BITONAL:
    case ExportPage::PALETTE:
    case ExportPage::IMAGE: {
      const QImage image = loadImage(page.mainFile(), errors);
      if (image.isNull()) {
        return false;
      }
      const QSizeF perPoint = pixelsPerPoint(image);
      result->size = QSizeF(image.width() / perPoint.width(), image.height() / perPoint.height());
      const QRectF rect(QPointF(0, 0), result->size);

      if (options.formats.pdf) {
        PdfWriter::Image pdfImage;
        // The kind was determined from the file header; decide by the actual image to be safe.
        bool ok;
        if (PdfImageEncoder::isBitonal(image)) {
          ok = makeBitonalImage(image, page.mainFile(), false, options.pdf.bitonalCompression, rect, &pdfImage, errors);
        } else if (PdfImageEncoder::isPalette(image)) {
          ok = makeColorImage(image, page.mainFile(), options.pdf.paletteCompression, options.pdf.paletteQuality, rect,
                              &pdfImage, errors);
        } else {
          ok = makeColorImage(image, page.mainFile(), options.pdf.colorCompression, options.pdf.colorQuality, rect,
                              &pdfImage, errors);
        }
        if (!ok) {
          return false;
        }
        result->images.push_back(std::move(pdfImage));
      }
      return recognizeText(image, page.mainFile(), page, position, perPoint, options, ocrEngine, cancel, result, texts,
                           errors);
    }
    case ExportPage::MRC: {
      const QImage foreground = loadImage(page.foregroundFile(), errors);
      if (foreground.isNull()) {
        return false;
      }
      const QSizeF perPoint = pixelsPerPoint(foreground);
      result->size = QSizeF(foreground.width() / perPoint.width(), foreground.height() / perPoint.height());

      if (!options.formats.pdf) {
        // Without a PDF the background isn't loaded, and so isn't compared below. The text formats
        // name the output file for the coordinates of the foreground, so it must have the same size.
        QSize mainSize;
        if (!PdfImageEncoder::readTiffInfo(page.mainFile(), &mainSize, nullptr, nullptr, nullptr)
            || (mainSize != foreground.size())) {
          errors->push_back(describeFailure(tr("The split output files of %1 differ in size. Process the page again."),
                                            page.mainFile(), QStringList()));
          return false;
        }
      } else {
        QImage background = loadImage(page.backgroundFile(), errors);
        if (background.isNull()) {
          return false;
        }
        // The page was checked when the dialog opened, but the files may have been written again since.
        if (background.size() != foreground.size()) {
          errors->push_back(describeFailure(tr("The split output files of %1 differ in size. Process the page again."),
                                            page.mainFile(), QStringList()));
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
          const int scale = std::max(1, options.pdf.backgroundScale);
          if (scale > 1) {
            const int width = std::max(1, static_cast<int>(std::ceil(part.width() / static_cast<double>(scale))));
            const int height = std::max(1, static_cast<int>(std::ceil(part.height() / static_cast<double>(scale))));
            part = palette ? scalePaletteImage(part, width, height)
                           : part.scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
          }
          PdfWriter::Image pdfImage;
          const PdfCompression compression = palette ? options.pdf.paletteCompression : options.pdf.colorCompression;
          const int quality = palette ? options.pdf.paletteQuality : options.pdf.colorQuality;
          if (!makeColorImage(part, page.backgroundFile(), compression, quality, toPoints(QRectF(content), perPoint),
                              &pdfImage, errors)) {
            return false;
          }
          result->images.push_back(std::move(pdfImage));
        }
        background = QImage();  // Pages are large; it isn't needed while the mask is compressed.

        PdfWriter::Image mask;
        if (!makeBitonalImage(foreground, page.foregroundFile(), true, options.pdf.bitonalCompression,
                              QRectF(QPointF(0, 0), result->size), &mask, errors)) {
          return false;
        }
        result->images.push_back(std::move(mask));
      }
      // The foreground holds only the text, without the pictures, which helps recognition. So
      // hOCR and ALTO have no picture areas for these pages, unlike for pages stored as one image.
      return recognizeText(foreground, page.foregroundFile(), page, position, perPoint, options, ocrEngine, cancel,
                           result, texts, errors);
    }
  }
  return false;
}  // ExportJob::preparePage

void ExportJob::run() {
  m_succeeded = false;
  m_cancelled = false;
  m_errors.clear();

  const int pageCount = static_cast<int>(m_pages.size());
  if (pageCount == 0) {
    m_errors.push_back(tr("No pages are selected."));
    return;
  }

  const ProcessingOptions::Formats& formats = m_options.formats;
  if (!formats.any()) {
    m_errors.push_back(tr("No format is selected."));
    return;
  }
  if (formats.hasTextFormat() && !m_options.ocr) {
    m_errors.push_back(tr("Text recognition isn't initialized."));
    return;
  }

  std::unique_ptr<QSaveFile> file;
  std::unique_ptr<PdfWriter> writer;
  bool ok = true;
  if (formats.pdf) {
    file = std::make_unique<QSaveFile>(m_paths.pdf);
    if (!file->open(QIODevice::WriteOnly)) {
      m_errors.push_back(describeFailure(tr("Could not create %1."), m_paths.pdf, QStringList(file->errorString())));
      return;
    }
    writer = std::make_unique<PdfWriter>(*file);
    writer->setProducer(QStringLiteral("ScanTailor OCR"));
    writer->setPdfA(m_options.pdf.pdfA);
    ok = writer->begin();
  }

  OcrTextOutputs::Settings textSettings;
  textSettings.txtFile = formats.txt ? m_paths.txt : QString();
  textSettings.hocrFile = formats.hocr ? m_paths.hocr : QString();
  textSettings.altoDir = formats.alto ? m_paths.altoDir : QString();
  textSettings.pageCount = pageCount;
  textSettings.tesseractVersion = OcrEngine::tesseractVersion();
  OcrTextOutputs textOutputs(textSettings);
  QString textError;
  if (!textOutputs.begin(&textError)) {
    m_errors.push_back(textError);
    return;
  }

  struct Prepared {
    bool ok = false;
    PdfWriter::Page page;
    OcrPageTexts texts;
    QStringList errors;

    qint64 bytes() const {
      qint64 sum = texts.bytes();
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
    // The pages prepared side by side share the threads of the image filters and decoders.
    const ThreadBudget::TaskScope taskScope;
    // Each thread has its own recognizer, created when it prepares its first page.
    std::unique_ptr<OcrEngine> ocrEngine;
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
          if (m_options.runsTextRecognition() && !ocrEngine && ocrInitError.isEmpty()) {
            auto newEngine = std::make_unique<OcrEngine>();
            if (newEngine->init(m_options.ocrDataDir, m_options.ocrLanguages, &ocrInitError)) {
              ocrEngine = std::move(newEngine);
            } else if (ocrInitError.isEmpty()) {
              ocrInitError = tr("Text recognition could not be started.");
            }
          }
          if (!ocrInitError.isEmpty()) {
            item.ok = false;
            item.errors.push_back(ocrInitError);
          } else {
            item.ok = preparePage(m_pages[index], m_options, &item.page, &item.errors, ocrEngine.get(),
                                  &m_cancelRequested, index + 1, &item.texts);
          }
        } catch (...) {
          // An exception escaping a std::thread would terminate the program.
          item.ok = false;
          item.errors.push_back(currentExceptionMessage());
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
  // Writing can throw too (e.g. std::bad_alloc). The workers are stopped and joined in any case:
  // destroying a std::thread that still runs would end the program.
  try {
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
      if (writer && !writer->addPage(item.page)) {
        ok = false;
        break;
      }
      if (!textOutputs.addPage(QFileInfo(m_pages[nextToWrite - 1].mainFile()).fileName(), item.texts, &textError)) {
        m_errors.push_back(textError);
        ok = false;
        break;
      }
      emit progress(nextToWrite, pageCount);
    }
  } catch (...) {
    m_errors.push_back(currentExceptionMessage());
    ok = false;
  }

  {
    std::lock_guard<std::mutex> lock(mutex);
    stop = true;
  }
  condition.notify_all();
  for (std::thread& thread : threads) {
    thread.join();
  }

  // A cancel request can arrive after the last page left the queue: while that page is written, from its
  // progress signal (the command line tool passes Ctrl+C on there), or during finish(). No file has been
  // replaced yet, so it is still honored.
  const auto takeLateCancelRequest = [&]() {
    if (ok && m_cancelRequested) {
      m_cancelled = true;
    }
  };
  takeLateCancelRequest();

  // Everything is written completely before the first existing file is replaced.
  if (ok && !m_cancelled) {
    try {
      ok = !writer || writer->finish();
      if (ok && !textOutputs.finish(&textError)) {
        m_errors.push_back(textError);
        ok = false;
      }
    } catch (...) {
      m_errors.push_back(currentExceptionMessage());
      ok = false;
    }
  }
  takeLateCancelRequest();
  if (ok && !m_cancelled) {
    QStringList replaced;
    if (file) {
      if (!file->commit()) {
        m_errors.push_back(describeFailure(tr("Could not write %1."), m_paths.pdf, QStringList(file->errorString())));
        textOutputs.cancel();
        return;
      }
      replaced.push_back(QDir::toNativeSeparators(m_paths.pdf));
    }
    if (textOutputs.commit(&replaced, &textError)) {
      m_succeeded = true;
      return;
    }
    m_errors.push_back(textError);
    if (!replaced.isEmpty()) {
      m_errors.push_back(tr("These were already replaced: %1").arg(replaced.join(QStringLiteral(", "))));
    }
    return;
  }

  textOutputs.cancel();
  if (file) {
    file->cancelWriting();
    if (!m_cancelled && m_errors.isEmpty()) {
      m_errors.push_back(describeFailure(tr("Could not write %1."), m_paths.pdf, QStringList(file->errorString())));
    }
  }
}  // ExportJob::run
