// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ScanFolderWatcher.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QMutexLocker>
#include <QRunnable>

#include "ImageLoader.h"
#include "ImageMetadataLoader.h"

namespace {
// While new files wait to be completely written, the directory is checked this often.
// Together with NewScanTracker::STABLE_CHECKS this decides how soon a scan shows up.
const int PENDING_CHECK_MS = 200;
// Otherwise it is checked this often, in case a change notification got lost.
const int IDLE_CHECK_MS = 2000;
}  // namespace

class ScanFolderWatcher::LoadTask : public QRunnable {
 public:
  LoadTask(ScanFolderWatcher* owner, int session, const QString& filePath)
      : m_owner(owner), m_session(session), m_filePath(filePath) {}

  void run() override {
    LoadResult result;
    result.session = m_session;
    result.filePath = m_filePath;
    const ImageMetadataLoader::Status status = ImageMetadataLoader::load(
        m_filePath, [&](const ImageMetadata& metadata) { result.metadata.push_back(metadata); });
    // Loading the whole image makes sure the scanning program has finished writing it.
    // A TIFF file, for instance, gets its directory at the end.
    result.ok = (status == ImageMetadataLoader::LOADED) && !result.metadata.empty()
                && !ImageLoader::load(m_filePath, 0).isNull();
    m_owner->addLoadResult(result);
  }

 private:
  // The owner waits for all tasks before it is destroyed.
  ScanFolderWatcher* m_owner;
  int m_session;
  QString m_filePath;
};

ScanFolderWatcher::ScanFolderWatcher(QObject* parent) : QObject(parent) {
  m_loadPool.setMaxThreadCount(2);
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &ScanFolderWatcher::check);
  connect(&m_fsWatcher, &QFileSystemWatcher::directoryChanged, this, [this]() {
    // Notifications come in bursts while a file is written; checking at a steady pace
    // keeps the "unchanged for a while" test meaningful.
    if (!m_timer.isActive() || (m_timer.remainingTime() > PENDING_CHECK_MS)) {
      scheduleCheck(PENDING_CHECK_MS);
    }
  });
}

ScanFolderWatcher::~ScanFolderWatcher() {
  stop();
  m_loadPool.waitForDone();
}

bool ScanFolderWatcher::isImportableImage(const QFileInfo& file) {
  static const QSet<QString> extensions{"png", "jpg", "jpeg", "tif", "tiff",
                                        // JPEG 2000: JP2 files, JPX / JPH files and raw codestreams.
                                        "jp2", "j2k", "j2c", "jpc", "jpf", "jpx", "jph", "jhc"};
  return extensions.contains(file.suffix().toLower());
}

QSet<QString> ScanFolderWatcher::imagesIn(const QString& dir) {
  QSet<QString> images;
  for (const QFileInfo& file : QDir(dir).entryInfoList(QDir::Files)) {
    if (isImportableImage(file)) {
      images.insert(file.absoluteFilePath());
    }
  }
  return images;
}

void ScanFolderWatcher::start(const QString& dir) {
  stop();
  m_dir = QDir(dir).absolutePath();
  m_tracker.reset(imagesIn(m_dir));
  m_fsWatcher.addPath(m_dir);
  m_clock.start();
  scheduleCheck(IDLE_CHECK_MS);
}

void ScanFolderWatcher::stop() {
  if (!m_dir.isEmpty()) {
    m_fsWatcher.removePath(m_dir);
  }
  m_dir.clear();
  m_timer.stop();
  // Images still being loaded belong to the old session and are ignored.
  ++m_session;
}

void ScanFolderWatcher::ignore(const QString& filePath) {
  m_tracker.done(QFileInfo(filePath).absoluteFilePath());
}

void ScanFolderWatcher::scheduleCheck(const int delayMs) {
  m_timer.start(delayMs);
}

void ScanFolderWatcher::check() {
  if (!isWatching()) {
    return;
  }

  std::vector<NewScanTracker::FileState> listing;
  for (const QFileInfo& file : QDir(m_dir).entryInfoList(QDir::Files)) {
    if (isImportableImage(file)) {
      NewScanTracker::FileState state;
      state.path = file.absoluteFilePath();
      state.size = file.size();
      state.modified = file.lastModified();
      listing.push_back(state);
    }
  }

  for (const QString& filePath : m_tracker.update(listing, m_clock.elapsed())) {
    startLoading(filePath);
  }
  scheduleCheck(m_tracker.hasPending() ? PENDING_CHECK_MS : IDLE_CHECK_MS);
}

void ScanFolderWatcher::startLoading(const QString& filePath) {
  m_loadPool.start(new LoadTask(this, m_session, filePath));
}

void ScanFolderWatcher::addLoadResult(const LoadResult& result) {
  {
    const QMutexLocker locker(&m_resultsMutex);
    m_results.push_back(result);
  }
  QMetaObject::invokeMethod(this, "processLoadResults", Qt::QueuedConnection);
}

void ScanFolderWatcher::processLoadResults() {
  std::vector<LoadResult> results;
  {
    const QMutexLocker locker(&m_resultsMutex);
    results.swap(m_results);
  }

  for (const LoadResult& result : results) {
    if (result.session != m_session) {
      // Watching was stopped or started again in between.
      continue;
    }
    if (result.ok) {
      m_tracker.done(result.filePath);
      emit imageReady(ImageFileInfo(QFileInfo(result.filePath), result.metadata));
    } else if (m_tracker.loadFailed(result.filePath, m_clock.elapsed())) {
      emit imageFailed(result.filePath);
    }
  }
  if (isWatching()) {
    scheduleCheck(m_tracker.hasPending() ? PENDING_CHECK_MS : IDLE_CHECK_MS);
  }
}
