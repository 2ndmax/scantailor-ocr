// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TessdataDownloader.h"

#include <QtNetwork/qtnetworkglobal.h>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkProxyFactory>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QUrl>
#include <mutex>

#if QT_CONFIG(ssl)
#include <QSslSocket>
#endif

#include "OcrLanguages.h"

namespace {
const QString kListingUrl = QStringLiteral("https://api.github.com/repos/tesseract-ocr/tessdata_best/contents/");
const QString kSuffix = QStringLiteral(".traineddata");
}  // namespace

TessdataDownloader::TessdataDownloader(QObject* parent) : QObject(parent), m_network(new QNetworkAccessManager(this)) {
  // Use the proxy settings of the system, as needed in many company and school networks.
  QNetworkProxyFactory::setUseSystemConfiguration(true);

#if QT_CONFIG(ssl) && defined(Q_OS_WIN)
  // Qt prefers its OpenSSL backend, but OpenSSL isn't shipped with the program.
  // Windows' own TLS implementation (Schannel) needs no extra files and uses the
  // certificates of the system.  This only works before the first TLS connection.
  static std::once_flag once;
  std::call_once(once, [] {
    const QString schannel = QStringLiteral("schannel");
    if (QSslSocket::availableBackends().contains(schannel)) {
      QSslSocket::setActiveBackend(schannel);
    }
  });
#endif
}

TessdataDownloader::~TessdataDownloader() {
  if (m_reply) {
    // Don't let the aborted reply call back into a half destroyed object.
    m_reply->disconnect(this);
    m_reply->abort();
    m_reply->deleteLater();
  }
  if (m_file) {
    m_file->cancelWriting();
  }
}

QByteArray TessdataDownloader::gitBlobSha1(const QByteArray& content) {
  QCryptographicHash hash(QCryptographicHash::Sha1);
  hash.addData("blob " + QByteArray::number(content.size()) + '\0');
  hash.addData(content);
  return hash.result().toHex();
}

QNetworkReply* TessdataDownloader::get(const QString& url, const bool githubApi) {
  QNetworkRequest request{QUrl(url)};
  // GitHub rejects requests without a user agent.
  request.setRawHeader("User-Agent", "ScanTailor-OCR");
  if (githubApi) {
    request.setRawHeader("Accept", "application/vnd.github+json");
  }
  // The download URLs redirect to another host.
  request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
  return m_network->get(request);
}

QString TessdataDownloader::replyError(QNetworkReply* reply) {
  const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  if (((status == 403) || (status == 429)) && (reply->rawHeader("X-RateLimit-Remaining") == "0")) {
    return tr(
        "GitHub allows only a limited number of requests per hour without signing in.  Please try again "
        "later.");
  }
  return reply->errorString();
}

/*================================== Listing ==================================*/

void TessdataDownloader::fetchList() {
  if (isBusy()) {
    return;
  }
  m_cancelled = false;
  m_listedFiles.clear();
  // The languages, then the script models.
  m_pendingListings = QStringList{QString(), QStringLiteral("script")};
  requestListing(m_pendingListings.takeFirst());
}

void TessdataDownloader::requestListing(const QString& path) {
  m_reply = get(kListingUrl + path, true);
  m_reply->setProperty("listingPath", path);
  connect(m_reply, &QNetworkReply::finished, this, &TessdataDownloader::listingFinished);
}

void TessdataDownloader::listingFinished() {
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  if (!reply) {
    return;
  }
  reply->deleteLater();
  if (m_cancelled) {
    return;
  }

  const QString path = reply->property("listingPath").toString();
  if (reply->error() != QNetworkReply::NoError) {
    if (path.isEmpty()) {
      emit listReady(std::vector<TessdataFile>(), replyError(reply));
      return;
    }
    // The script models are optional; offer the languages anyway.
  } else {
    const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
    if (!doc.isArray() && path.isEmpty()) {
      emit listReady(std::vector<TessdataFile>(), tr("GitHub sent an unexpected answer."));
      return;
    }
    const QString codePrefix = path.isEmpty() ? QString() : path + '/';
    for (const QJsonValue& value : doc.array()) {
      const QJsonObject obj = value.toObject();
      const QString name = obj.value("name").toString();
      if ((obj.value("type").toString() != "file") || !name.endsWith(kSuffix)) {
        continue;
      }
      TessdataFile file;
      file.code = codePrefix + name.chopped(kSuffix.size());
      file.url = obj.value("download_url").toString();
      file.sha = obj.value("sha").toString().toLatin1();
      file.size = static_cast<qint64>(obj.value("size").toDouble());
      if (file.url.isEmpty() || file.sha.isEmpty() || !OcrLanguages::isRecognitionModel(file.code)) {
        continue;
      }
      m_listedFiles.push_back(file);
    }
  }

  if (!m_pendingListings.isEmpty()) {
    requestListing(m_pendingListings.takeFirst());
    return;
  }
  emit listReady(m_listedFiles, QString());
}  // TessdataDownloader::listingFinished

/*================================= Downloading =================================*/

void TessdataDownloader::download(const std::vector<TessdataFile>& files, const QString& targetDir) {
  if (isBusy()) {
    return;
  }
  m_cancelled = false;
  m_queue.assign(files.begin(), files.end());
  m_targetDir = targetDir;
  m_downloaded.clear();
  m_bytesTotal = 0;
  for (const TessdataFile& file : files) {
    m_bytesTotal += file.size;
  }
  m_bytesDone = 0;
  startNextDownload();
}

void TessdataDownloader::startNextDownload() {
  if (m_queue.empty()) {
    finishDownloads(QString());
    return;
  }
  const TessdataFile& file = m_queue.front();
  const QString target = OcrLanguages::filePath(m_targetDir, file.code);
  if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
    finishDownloads(
        tr("Could not create the folder %1.").arg(QDir::toNativeSeparators(QFileInfo(target).absolutePath())));
    return;
  }
  // QSaveFile writes to a temporary file and only renames it on commit().
  m_file = std::make_unique<QSaveFile>(target);
  if (!m_file->open(QIODevice::WriteOnly)) {
    const QString reason = m_file->errorString();
    m_file.reset();
    finishDownloads(tr("Could not write %1: %2").arg(QDir::toNativeSeparators(target), reason));
    return;
  }
  m_hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha1);
  m_hash->addData("blob " + QByteArray::number(file.size) + '\0');
  m_bytesReceived = 0;

  m_reply = get(file.url, false);
  connect(m_reply, &QNetworkReply::readyRead, this, &TessdataDownloader::downloadReadyRead);
  connect(m_reply, &QNetworkReply::downloadProgress, this,
          [this](const qint64 received, qint64) { emit progress(m_bytesDone + received, m_bytesTotal); });
  connect(m_reply, &QNetworkReply::finished, this, &TessdataDownloader::downloadFinished);
}

void TessdataDownloader::downloadReadyRead() {
  if (!m_reply || !m_file) {
    return;
  }
  const QByteArray data = m_reply->readAll();
  m_bytesReceived += data.size();
  m_hash->addData(data);
  if (m_file->write(data) != data.size()) {
    // Reported in downloadFinished() through the file's error state.
    m_reply->abort();
  }
}

void TessdataDownloader::downloadFinished() {
  if (m_reply && (m_reply->error() == QNetworkReply::NoError) && (m_reply->bytesAvailable() > 0)) {
    downloadReadyRead();
  }
  QNetworkReply* reply = m_reply;
  m_reply = nullptr;
  if (!reply || m_queue.empty()) {
    return;
  }
  reply->deleteLater();
  const TessdataFile file = m_queue.front();

  if (m_cancelled) {
    m_file->cancelWriting();
    finishDownloads(QString());
    return;
  }
  if (m_file->error() != QFileDevice::NoError) {
    const QString reason = m_file->errorString();
    m_file->cancelWriting();
    finishDownloads(tr("Could not write the language file of \"%1\": %2").arg(file.code, reason));
    return;
  }
  if (reply->error() != QNetworkReply::NoError) {
    m_file->cancelWriting();
    finishDownloads(tr("Downloading \"%1\" failed: %2").arg(file.code, replyError(reply)));
    return;
  }
  if ((m_bytesReceived != file.size) || (m_hash->result().toHex() != file.sha)) {
    m_file->cancelWriting();
    finishDownloads(tr("The downloaded file of \"%1\" is incomplete or damaged.  Please try again.").arg(file.code));
    return;
  }
  if (!m_file->commit()) {
    finishDownloads(tr("Could not write the language file of \"%1\": %2").arg(file.code, m_file->errorString()));
    return;
  }

  m_file.reset();
  m_downloaded.push_back(file.code);
  m_bytesDone += file.size;
  m_queue.pop_front();
  emit progress(m_bytesDone, m_bytesTotal);
  startNextDownload();
}  // TessdataDownloader::downloadFinished

void TessdataDownloader::finishDownloads(const QString& error) {
  m_queue.clear();
  // An uncommitted QSaveFile discards its temporary file.
  m_file.reset();
  m_hash.reset();
  emit finished(m_downloaded, error, m_cancelled);
}

void TessdataDownloader::cancel() {
  m_cancelled = true;
  if (m_reply) {
    // Leads to listingFinished() or downloadFinished().
    m_reply->abort();
  }
}
