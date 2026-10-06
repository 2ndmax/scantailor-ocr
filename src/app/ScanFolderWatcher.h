// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_SCANFOLDERWATCHER_H_
#define SCANTAILOR_APP_SCANFOLDERWATCHER_H_

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QMutex>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>
#include <QTimer>
#include <vector>

#include "ImageFileInfo.h"
#include "NewScanTracker.h"

class QFileInfo;

/**
 * \brief Watches a directory for new images, e.g. saved there by a scanning program.
 *
 * Images that are there when watching starts are left alone.  A new image is reported
 * once it is completely written: its size stayed the same for a moment and it can be
 * loaded.  The loading is done in a background thread.  Subdirectories are not watched.
 */
class ScanFolderWatcher : public QObject {
  Q_OBJECT
 public:
  explicit ScanFolderWatcher(QObject* parent = nullptr);

  ~ScanFolderWatcher() override;

  /** Whether ScanTailor can import the file, judged by its extension. */
  static bool isImportableImage(const QFileInfo& file);

  /** The importable images in \p dir, as absolute paths. */
  static QSet<QString> imagesIn(const QString& dir);

  /** Starts watching \p dir.  The images in it now are not reported. */
  void start(const QString& dir);

  void stop();

  bool isWatching() const { return !m_dir.isEmpty(); }

  const QString& directory() const { return m_dir; }

 signals:

  void imageReady(const ImageFileInfo& file);

  /** The image couldn't be loaded, not even after waiting for a while. */
  void imageFailed(const QString& filePath);

 private slots:

  void check();

  /** Called in the main thread after an image was loaded in the background. */
  void processLoadResults();

 private:
  struct LoadResult {
    int session = 0;
    QString filePath;
    std::vector<ImageMetadata> metadata;
    bool ok = false;
  };

  class LoadTask;

  void scheduleCheck(int delayMs);

  void startLoading(const QString& filePath);

  void addLoadResult(const LoadResult& result);

  QString m_dir;
  int m_session = 0;
  QMutex m_resultsMutex;
  std::vector<LoadResult> m_results;
  QFileSystemWatcher m_fsWatcher;
  QTimer m_timer;
  QElapsedTimer m_clock;
  NewScanTracker m_tracker;
  QThreadPool m_loadPool;
};

#endif  // SCANTAILOR_APP_SCANFOLDERWATCHER_H_
