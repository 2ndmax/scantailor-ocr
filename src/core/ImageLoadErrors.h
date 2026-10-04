// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGELOADERRORS_H_
#define SCANTAILOR_CORE_IMAGELOADERRORS_H_

#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "NonCopyable.h"

/**
 * \brief Collects the diagnostic messages that image decoding libraries
 *        (libtiff, OpenJPEG, Qt) emit on the current thread.
 *
 * The libraries report problems through global or per-codec callbacks.
 * Those callbacks forward the messages to addError() / addWarning(), which
 * route them to the innermost capture that is active on the calling thread.
 * Messages emitted while no capture is active are only logged.
 */
class ImageLoadErrorCapture {
  DECLARE_NON_COPYABLE(ImageLoadErrorCapture)

 public:
  ImageLoadErrorCapture();

  ~ImageLoadErrorCapture();

  /** May be called from any thread. */
  static void addError(const QString& message);

  /** May be called from any thread. */
  static void addWarning(const QString& message);

  /**
   * \brief The collected errors, or if there were none, the collected warnings.
   *
   * Duplicates are removed and the number of messages is limited.
   */
  QStringList messages() const;

 private:
  static void append(QStringList& list, const QString& message);

  ImageLoadErrorCapture* m_prev;
  QStringList m_errors;
  QStringList m_warnings;
};


/**
 * \brief Distributes "image could not be loaded / written" events to the GUI.
 *
 * Each file / page combination is reported only once per session, so that
 * an image that is both thumbnailed and loaded doesn't produce two reports.
 */
class ImageLoadErrorReporter : public QObject {
  Q_OBJECT
  DECLARE_NON_COPYABLE(ImageLoadErrorReporter)

 public:
  static ImageLoadErrorReporter& instance();

  /**
   * May be called from any thread.
   *
   * \param filePath The image file.
   * \param page Zero for single-page files, otherwise the 1-based page number
   *        (the same convention as ImageId::page()).
   * \param messages The reasons of the failure, as collected by ImageLoadErrorCapture.
   */
  void report(const QString& filePath, int page, const QStringList& messages);

  /**
   * \brief Reports an output file that couldn't be written.
   *
   * May be called from any thread.  Unlike load failures, these aren't
   * deduplicated, as writing the same file may fail again after a retry.
   */
  void reportWriteFailure(const QString& filePath, const QStringList& messages);

  /**
   * \brief Reports a page whose processing failed with an unexpected error.
   *
   * May be called from any thread.  Not deduplicated, like write failures.
   * \p filePath may be empty if the page is unknown.
   */
  void reportProcessingFailure(const QString& filePath, int page, const QStringList& messages);

 signals:
  /**
   * Emitted from the thread calling report().  Receivers living in another
   * thread (like the GUI) get it through a queued connection.
   */
  void imageLoadFailed(const QString& filePath, int page, const QStringList& messages);

  /** Emitted from the thread calling reportWriteFailure(). */
  void imageWriteFailed(const QString& filePath, const QStringList& messages);

  /** Emitted from the thread calling reportProcessingFailure(). */
  void pageProcessingFailed(const QString& filePath, int page, const QStringList& messages);

 private:
  ImageLoadErrorReporter() = default;

  QMutex m_mutex;
  QSet<QString> m_reported;
};


#endif  // ifndef SCANTAILOR_CORE_IMAGELOADERRORS_H_
