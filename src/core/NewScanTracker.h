// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_NEWSCANTRACKER_H_
#define SCANTAILOR_CORE_NEWSCANTRACKER_H_

#include <QDateTime>
#include <QSet>
#include <QString>
#include <map>
#include <vector>

/**
 * \brief Decides when a new file in a watched directory is ready to be imported.
 *
 * Files that were there when watching started are never imported.  A new file is a
 * candidate once its size and modification time stayed the same over a few checks.
 * The caller then tries to load it and reports back: a file that can't be loaded yet
 * (because the scanning program is still writing it) is tried again, until it is
 * given up after some time.
 *
 * Contains no timers and no file access, so it can be tested.  Times are in milliseconds.
 */
class NewScanTracker {
 public:
  struct FileState {
    QString path;
    qint64 size = 0;
    QDateTime modified;
  };

  /** Checks in a row a file must stay unchanged to become a candidate. */
  static constexpr int STABLE_CHECKS = 2;

  /** A file that still can't be loaded this long after it appeared is given up. */
  static constexpr qint64 GIVE_UP_MS = 60000;

  /** Starts over.  The files in \p existing are never imported. */
  void reset(const QSet<QString>& existing);

  /**
   * Takes the current listing of the directory and returns the files that should be
   * loaded now.  They are not returned again until reported with loadFailed().
   */
  std::vector<QString> update(const std::vector<FileState>& listing, qint64 nowMs);

  /** The file was imported, or rejected for another reason, and is to be ignored from now on. */
  void done(const QString& path);

  /**
   * The file couldn't be loaded.  Returns true if it is given up (and ignored from now on),
   * false if it will be tried again once it is unchanged for a while.
   */
  bool loadFailed(const QString& path, qint64 nowMs);

  /** Whether there are new files that are not imported or given up yet. */
  bool hasPending() const { return !m_pending.empty(); }

 private:
  struct Pending {
    qint64 size = 0;
    QDateTime modified;
    int stableChecks = 0;
    qint64 firstSeenMs = 0;
    bool loading = false;
  };

  QSet<QString> m_ignored;
  std::map<QString, Pending> m_pending;
};

#endif  // SCANTAILOR_CORE_NEWSCANTRACKER_H_
