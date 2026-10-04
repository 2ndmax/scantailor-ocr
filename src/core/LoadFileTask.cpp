// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LoadFileTask.h"

#include <imageproc/Grayscale.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextDocument>
#include <exception>
#include <new>

#include "AbstractFilter.h"
#include "Dpm.h"
#include "ErrorWidget.h"
#include "FilterData.h"
#include "FilterOptionsWidget.h"
#include "FilterUiInterface.h"
#include "ImageLoadErrors.h"
#include "ImageLoader.h"
#include "ProjectPages.h"
#include "ThumbnailPixmapCache.h"
#include "filters/fix_orientation/Task.h"

using namespace imageproc;

class LoadFileTask::ErrorResult : public FilterResult {
  Q_DECLARE_TR_FUNCTIONS(LoadFileTask)
 public:
  /**
   * \param processingFailed Whether the file was loaded, but processing it failed
   *        with an unexpected error, rather than the file failing to load.
   */
  ErrorResult(const QString& filePath, const QStringList& reasons, bool processingFailed = false);

  void updateUI(FilterUiInterface* ui) override;

  std::shared_ptr<AbstractFilter> filter() override { return nullptr; }

 private:
  QString m_filePath;
  QStringList m_reasons;
  bool m_fileExists;
  bool m_processingFailed;
};


LoadFileTask::LoadFileTask(Type type,
                           const PageInfo& page,
                           std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                           std::shared_ptr<ProjectPages> pages,
                           std::shared_ptr<fix_orientation::Task> nextTask)
    : BackgroundTask(type),
      m_thumbnailCache(std::move(thumbnailCache)),
      m_imageId(page.imageId()),
      m_imageMetadata(page.metadata()),
      m_pages(std::move(pages)),
      m_nextTask(std::move(nextTask)) {
  assert(m_nextTask);
}

LoadFileTask::~LoadFileTask() = default;

FilterResultPtr LoadFileTask::operator()() {
  QStringList loadErrors;
  QImage image = ImageLoader::load(m_imageId, &loadErrors);

  try {
    throwIfCancelled();

    if (image.isNull()) {
      return std::make_shared<ErrorResult>(m_imageId.filePath(), loadErrors);
    } else {
      convertToSupportedFormat(image);
      updateImageSizeIfChanged(image);
      overrideDpi(image);
      m_thumbnailCache->ensureThumbnailExists(m_imageId, image);
      return m_nextTask->process(*this, FilterData(image));
    }
  } catch (const CancelledException&) {
    return nullptr;
  } catch (const std::bad_alloc&) {
    throw;  // Handled by the out-of-memory handler of the thread pool.
  } catch (const std::exception& e) {
    // An unexpected error in one of the filters. Report it and show it in place of
    // the page, instead of letting it terminate the whole program.
    return processingFailed(QString::fromUtf8(e.what()));
  } catch (...) {
    return processingFailed(QCoreApplication::translate("LoadFileTask", "Unknown error."));
  }
}

FilterResultPtr LoadFileTask::processingFailed(const QString& reason) const {
  const QStringList reasons(reason);
  ImageLoadErrorReporter::instance().reportProcessingFailure(m_imageId.filePath(), m_imageId.page(), reasons);
  return std::make_shared<ErrorResult>(m_imageId.filePath(), reasons, true);
}

void LoadFileTask::updateImageSizeIfChanged(const QImage& image) {
  // The user might just replace a file with another one.
  // In that case, we update its size that we store.
  // Note that we don't do the same about DPI, because
  // a DPI mismatch between the image and the stored value
  // may indicate that the DPI was overridden.
  // TODO: do something about DPIs when we have the ability
  // to change DPIs at any point in time (not just when
  // creating a project).
  if (image.size() != m_imageMetadata.size()) {
    m_imageMetadata.setSize(image.size());
    m_pages->updateImageMetadata(m_imageId, m_imageMetadata);
  }
}

void LoadFileTask::overrideDpi(QImage& image) const {
  // Beware: QImage will have a default DPI when loading
  // an image that doesn't specify one.
  const Dpm dpm(m_imageMetadata.dpi());
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
}

void LoadFileTask::convertToSupportedFormat(QImage& image) const {
  if (((image.format() == QImage::Format_Indexed8) && !image.isGrayscale()) || (image.depth() > 8)) {
    const QImage::Format fmt = image.hasAlphaChannel() ? QImage::Format_ARGB32 : QImage::Format_RGB32;
    image = image.convertToFormat(fmt);
  } else {
    image = toGrayscale(image);
  }
}

/*======================= LoadFileTask::ErrorResult ======================*/

LoadFileTask::ErrorResult::ErrorResult(const QString& filePath, const QStringList& reasons, const bool processingFailed)
    : m_filePath(QDir::toNativeSeparators(filePath)),
      m_reasons(reasons),
      m_fileExists(QFile::exists(filePath)),
      m_processingFailed(processingFailed) {}

void LoadFileTask::ErrorResult::updateUI(FilterUiInterface* ui) {
  class ErrWidget : public ErrorWidget {
   public:
    ErrWidget(std::shared_ptr<AbstractCommand<void>> relinkingDialogRequester,
              const QString& text,
              Qt::TextFormat fmt = Qt::AutoText)
        : ErrorWidget(text, fmt), m_relinkingDialogRequester(std::move(relinkingDialogRequester)) {}

   private:
    void linkActivated(const QString&) override { (*m_relinkingDialogRequester)(); }

    std::shared_ptr<AbstractCommand<void>> m_relinkingDialogRequester;
  };


  QString errMsg;
  Qt::TextFormat fmt = Qt::AutoText;
  if (m_processingFailed) {
    errMsg = tr("This page could not be processed because of an unexpected error:\n%1").arg(m_filePath);
    if (!m_reasons.isEmpty()) {
      errMsg += QLatin1String("\n\n") + tr("Reason:") + QLatin1Char('\n') + m_reasons.join(QLatin1Char('\n'));
    }
    errMsg += QLatin1String("\n\n")
              + tr(
                  "Changing the settings of this page or of a previous step may help. "
                  "Please report this error together with the steps that led to it.");
    fmt = Qt::PlainText;
  } else if (m_fileExists) {
    errMsg = tr("The following file could not be loaded:\n%1").arg(m_filePath);
    if (!m_reasons.isEmpty()) {
      errMsg += QLatin1String("\n\n") + tr("Reason:") + QLatin1Char('\n') + m_reasons.join(QLatin1Char('\n'));
    }
    fmt = Qt::PlainText;
  } else {
    errMsg = tr("The following file doesn't exist:<br>%1<br>"
                "<br>"
                "Use the <a href=\"#relink\">Relinking Tool</a> to locate it.")
                 .arg(m_filePath.toHtmlEscaped());
    fmt = Qt::RichText;
  }
  ui->setImageWidget(new ErrWidget(ui->relinkingDialogRequester(), errMsg, fmt), ui->TRANSFER_OWNERSHIP);
  ui->setOptionsWidget(new FilterOptionsWidget, ui->TRANSFER_OWNERSHIP);
}  // LoadFileTask::ErrorResult::updateUI
