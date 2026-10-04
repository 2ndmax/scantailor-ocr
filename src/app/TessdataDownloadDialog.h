// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_TESSDATADOWNLOADDIALOG_H_
#define SCANTAILOR_APP_TESSDATADOWNLOADDIALOG_H_

#include <QDialog>
#include <QStringList>
#include <vector>

#include "TessdataDownloader.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;

/**
 * \brief Lets the user download more OCR languages from GitHub (tessdata_best).
 *
 * The list of available files is requested from GitHub when the dialog opens.
 * Without an internet connection, the dialog just says so; the installed
 * languages can still be used.
 */
class TessdataDownloadDialog : public QDialog {
  Q_OBJECT
 public:
  explicit TessdataDownloadDialog(QWidget* parent = nullptr);

  ~TessdataDownloadDialog() override;

  /** The codes of the languages downloaded while the dialog was open. */
  const QStringList& downloadedCodes() const { return m_downloaded; }

 public slots:
  void reject() override;

 private slots:
  void listReady(const std::vector<TessdataFile>& files, const QString& error);

  void applyFilter();

  void startDownload();

  void downloadProgress(qint64 bytesDone, qint64 bytesTotal);

  void downloadFinished(const QStringList& downloaded, const QString& error, bool cancelled);

  void updateControls();

 private:
  void fillList();

  void setRunning(bool running);

  TessdataDownloader m_downloader;
  std::vector<TessdataFile> m_files;
  QString m_targetDir;
  QStringList m_downloaded;
  bool m_running = false;

  QLabel* m_targetLabel = nullptr;
  QLineEdit* m_filterEdit = nullptr;
  QListWidget* m_list = nullptr;
  QProgressBar* m_progressBar = nullptr;
  QLabel* m_statusLabel = nullptr;
  QPushButton* m_downloadButton = nullptr;
  QPushButton* m_closeButton = nullptr;
};


#endif  // SCANTAILOR_APP_TESSDATADOWNLOADDIALOG_H_
