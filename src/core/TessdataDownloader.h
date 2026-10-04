// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TESSDATADOWNLOADER_H_
#define SCANTAILOR_CORE_TESSDATADOWNLOADER_H_

#include <QByteArray>
#include <QCryptographicHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <deque>
#include <memory>
#include <vector>

class QNetworkAccessManager;
class QNetworkReply;
class QSaveFile;

/** A language file offered for download. */
struct TessdataFile {
  /** E.g. "deu", or "script/Latin" for script models. */
  QString code;
  QString url;
  /** Git's SHA-1 of the file, as hex string. */
  QByteArray sha;
  qint64 size = 0;
};


/**
 * \brief Lists and downloads language files from the tessdata_best repository on GitHub.
 *
 * Works asynchronously and has to be used from the GUI thread.  Downloaded files are
 * checked against the checksum GitHub lists for them.  A file only appears under its
 * final name once it has been downloaded completely and correctly.
 */
class TessdataDownloader : public QObject {
  Q_OBJECT
 public:
  explicit TessdataDownloader(QObject* parent = nullptr);

  ~TessdataDownloader() override;

  /** Requests the list of files.  Emits listReady() when done. */
  void fetchList();

  /**
   * \brief Downloads the files one after another into \p targetDir.
   *
   * Script models go into the "script" subfolder.  Emits progress() and, at the end, finished().
   */
  void download(const std::vector<TessdataFile>& files, const QString& targetDir);

  /** Stops the current list request or download. */
  void cancel();

  bool isBusy() const { return !m_reply.isNull(); }

  /** The SHA-1 git computes for a file ("blob <size>\0" followed by the content), as hex. */
  static QByteArray gitBlobSha1(const QByteArray& content);

 signals:
  /** \p error is empty on success. */
  void listReady(const std::vector<TessdataFile>& files, const QString& error);

  void progress(qint64 bytesDone, qint64 bytesTotal);

  /**
   * \param downloaded The codes of the files downloaded successfully.
   * \param error Empty on success and when cancelled.
   */
  void finished(const QStringList& downloaded, const QString& error, bool cancelled);

 private:
  QNetworkReply* get(const QString& url, bool githubApi);

  void requestListing(const QString& path);

  void listingFinished();

  void startNextDownload();

  void downloadReadyRead();

  void downloadFinished();

  void finishDownloads(const QString& error);

  static QString replyError(QNetworkReply* reply);

  QNetworkAccessManager* m_network;
  QPointer<QNetworkReply> m_reply;
  bool m_cancelled = false;

  // Listing.
  QStringList m_pendingListings;
  std::vector<TessdataFile> m_listedFiles;

  // Downloading.
  std::deque<TessdataFile> m_queue;
  QString m_targetDir;
  QStringList m_downloaded;
  qint64 m_bytesTotal = 0;
  qint64 m_bytesDone = 0;
  qint64 m_bytesReceived = 0;
  std::unique_ptr<QSaveFile> m_file;
  std::unique_ptr<QCryptographicHash> m_hash;
};


#endif  // SCANTAILOR_CORE_TESSDATADOWNLOADER_H_
