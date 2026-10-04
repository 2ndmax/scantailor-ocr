// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageLoadErrors.h"

#include <QDebug>
#include <QMutexLocker>

namespace {
// The innermost capture active on the current thread.
thread_local ImageLoadErrorCapture* t_currentCapture = nullptr;

// A broken file can easily make a decoder complain once per strip or tile.
// Nobody is going to read hundreds of those.
const int MAX_MESSAGES = 10;
}  // namespace

ImageLoadErrorCapture::ImageLoadErrorCapture() : m_prev(t_currentCapture) {
  t_currentCapture = this;
}

ImageLoadErrorCapture::~ImageLoadErrorCapture() {
  t_currentCapture = m_prev;
}

void ImageLoadErrorCapture::addError(const QString& message) {
  if (t_currentCapture) {
    append(t_currentCapture->m_errors, message);
  } else {
    qWarning().noquote() << message;
  }
}

void ImageLoadErrorCapture::addWarning(const QString& message) {
  if (t_currentCapture) {
    append(t_currentCapture->m_warnings, message);
  } else {
    qDebug().noquote() << message;
  }
}

QStringList ImageLoadErrorCapture::messages() const {
  return m_errors.isEmpty() ? m_warnings : m_errors;
}

void ImageLoadErrorCapture::append(QStringList& list, const QString& message) {
  const QString trimmed = message.trimmed();
  if (trimmed.isEmpty() || (list.size() >= MAX_MESSAGES) || list.contains(trimmed)) {
    return;
  }
  list.push_back(trimmed);
}

/*========================== ImageLoadErrorReporter ==========================*/

ImageLoadErrorReporter& ImageLoadErrorReporter::instance() {
  static ImageLoadErrorReporter reporter;
  return reporter;
}

void ImageLoadErrorReporter::report(const QString& filePath, const int page, const QStringList& messages) {
  {
    const QString key = filePath + QLatin1Char('\n') + QString::number(page);
    QMutexLocker locker(&m_mutex);
    if (m_reported.contains(key)) {
      return;
    }
    m_reported.insert(key);
  }

  qWarning().noquote() << "Failed to load image" << filePath << "page" << page << ":" << messages.join("; ");
  emit imageLoadFailed(filePath, page, messages);
}

void ImageLoadErrorReporter::reportWriteFailure(const QString& filePath, const QStringList& messages) {
  qWarning().noquote() << "Failed to write image" << filePath << ":" << messages.join("; ");
  emit imageWriteFailed(filePath, messages);
}

void ImageLoadErrorReporter::reportProcessingFailure(const QString& filePath,
                                                     const int page,
                                                     const QStringList& messages) {
  qCritical().noquote() << "Failed to process" << filePath << "page" << page << ":" << messages.join("; ");
  emit pageProcessingFailed(filePath, page, messages);
}
