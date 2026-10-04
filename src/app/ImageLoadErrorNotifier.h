// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_IMAGELOADERRORNOTIFIER_H_
#define SCANTAILOR_APP_IMAGELOADERRORNOTIFIER_H_

#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <vector>

class QMessageBox;
class QWidget;

/**
 * \brief Tells the user about images that couldn't be loaded, output files
 *        that couldn't be written, and pages whose processing failed.
 *
 * Failures typically come in bursts (a folder full of files in an unsupported
 * format), so they are collected for a moment and shown together in a single,
 * non-modal message box.  It names the first few files with the reason and
 * keeps the full list in its details section.  Failures that occur while the
 * box is open are added to it rather than opening another one.  The user can
 * mute the notifications for the rest of the session.
 */
class ImageLoadErrorNotifier : public QObject {
  Q_OBJECT
 public:
  explicit ImageLoadErrorNotifier(QWidget* parentWindow);

  ~ImageLoadErrorNotifier() override;

 private slots:
  void onImageLoadFailed(const QString& filePath, int page, const QStringList& messages);

  void onImageWriteFailed(const QString& filePath, const QStringList& messages);

  void onPageProcessingFailed(const QString& filePath, int page, const QStringList& messages);

  void showPending();

 private:
  enum class Kind { LOAD, WRITE, PROCESSING };

  struct Entry {
    QString filePath;
    int page;
    QStringList messages;
    Kind kind;
  };

  void addEntry(Entry entry);

  static QString describeFile(const Entry& entry, bool fullPath);

  void updateMessageBox();

  QWidget* m_parentWindow;
  QTimer m_delayTimer;
  QPointer<QMessageBox> m_messageBox;
  std::vector<Entry> m_entries;
  bool m_muted;
};


#endif  // ifndef SCANTAILOR_APP_IMAGELOADERRORNOTIFIER_H_
