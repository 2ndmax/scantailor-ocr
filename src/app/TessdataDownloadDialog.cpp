// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TessdataDownloadDialog.h"

#include <QDir>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <tuple>

#include "OcrLanguages.h"

namespace {
/** Marks the list item that separates the languages from the script models. */
const int kHeaderItem = -1;
}  // namespace

TessdataDownloadDialog::TessdataDownloadDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Download OCR languages"));

  auto* layout = new QVBoxLayout(this);
  auto* intro = new QLabel(
      tr("Language files of the \"tessdata_best\" collection of the Tesseract project, which give the best "
         "recognition.  Tick the languages you need and click \"Download\"."));
  intro->setWordWrap(true);
  layout->addWidget(intro);

  QString dirError;
  m_targetDir = OcrLanguages::downloadDir(&dirError);
  m_targetLabel = new QLabel(
      m_targetDir.isEmpty() ? dirError : tr("The files are saved in %1").arg(QDir::toNativeSeparators(m_targetDir)));
  m_targetLabel->setWordWrap(true);
  m_targetLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_targetLabel);

  m_filterEdit = new QLineEdit;
  m_filterEdit->setPlaceholderText(tr("Search, e.g. \"Deutsch\" or \"fra\""));
  m_filterEdit->setClearButtonEnabled(true);
  layout->addWidget(m_filterEdit);

  m_list = new QListWidget;
  m_list->setSelectionMode(QAbstractItemView::NoSelection);
  layout->addWidget(m_list, 1);

  m_progressBar = new QProgressBar;
  m_progressBar->setRange(0, 1000);
  m_progressBar->setValue(0);
  m_progressBar->setTextVisible(false);
  layout->addWidget(m_progressBar);

  m_statusLabel = new QLabel;
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_statusLabel);

  auto* buttons = new QHBoxLayout;
  buttons->addStretch(1);
  m_downloadButton = new QPushButton(tr("Download"));
  m_downloadButton->setDefault(true);
  m_closeButton = new QPushButton(tr("Close"));
  buttons->addWidget(m_downloadButton);
  buttons->addWidget(m_closeButton);
  layout->addLayout(buttons);

  connect(&m_downloader, &TessdataDownloader::listReady, this, &TessdataDownloadDialog::listReady);
  connect(&m_downloader, &TessdataDownloader::progress, this, &TessdataDownloadDialog::downloadProgress);
  connect(&m_downloader, &TessdataDownloader::finished, this, &TessdataDownloadDialog::downloadFinished);
  connect(m_filterEdit, &QLineEdit::textChanged, this, &TessdataDownloadDialog::applyFilter);
  connect(m_list, &QListWidget::itemChanged, this, &TessdataDownloadDialog::updateControls);
  connect(m_downloadButton, &QPushButton::clicked, this, &TessdataDownloadDialog::startDownload);
  connect(m_closeButton, &QPushButton::clicked, this, &TessdataDownloadDialog::reject);

  m_statusLabel->setText(tr("Loading the list of languages from GitHub ..."));
  m_downloader.fetchList();
  updateControls();
  resize(560, 620);
}

TessdataDownloadDialog::~TessdataDownloadDialog() = default;

void TessdataDownloadDialog::reject() {
  if (m_running) {
    // The first click cancels the download, the dialog closes after the next one.
    m_downloader.cancel();
    return;
  }
  m_downloader.cancel();
  QDialog::reject();
}

void TessdataDownloadDialog::listReady(const std::vector<TessdataFile>& files, const QString& error) {
  if (!error.isEmpty()) {
    m_statusLabel->setText(
        tr("The list of languages could not be loaded from GitHub: %1\n\nWithout an internet connection, the "
           "installed languages can still be used.  Language files can also be copied by hand into %2.")
            .arg(error, QDir::toNativeSeparators(OcrLanguages::programDir())));
    return;
  }
  m_files = files;
  m_statusLabel->clear();
  fillList();
}

void TessdataDownloadDialog::fillList() {
  const std::map<QString, QString> installed = OcrLanguages::available();

  // Languages first, then the script models, each sorted by name.
  std::vector<std::tuple<bool, QString, int>> entries;
  for (int i = 0; i < static_cast<int>(m_files.size()); ++i) {
    const QString& code = m_files[i].code;
    entries.emplace_back(OcrLanguages::isScript(code), OcrLanguages::displayName(code), i);
  }
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    if (std::get<0>(a) != std::get<0>(b)) {
      return !std::get<0>(a);
    }
    return QString::localeAwareCompare(std::get<1>(a), std::get<1>(b)) < 0;
  });

  const QSignalBlocker blocker(m_list);
  m_list->clear();
  bool headerAdded = false;
  const QString dash = QStringLiteral("  ") + QChar(0x2013) + QStringLiteral("  ");
  for (const auto& entry : entries) {
    const TessdataFile& file = m_files[std::get<2>(entry)];
    if (std::get<0>(entry) && !headerAdded) {
      auto* header = new QListWidgetItem(tr("Scripts (one model for all languages written in that script)"), m_list);
      QFont font = header->font();
      font.setBold(true);
      header->setFont(font);
      header->setFlags(Qt::ItemIsEnabled);
      header->setData(Qt::UserRole, kHeaderItem);
      headerAdded = true;
    }

    QString text = std::get<1>(entry) + dash + QLocale().formattedDataSize(file.size);
    auto* item = new QListWidgetItem(m_list);
    item->setData(Qt::UserRole, std::get<2>(entry));
    if (installed.count(file.code) > 0) {
      text += dash + tr("installed");
      // A ticked box that can't be unticked: keeps the names aligned and shows the state.
      item->setFlags(Qt::ItemIsEnabled);
      item->setCheckState(Qt::Checked);
    } else {
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
      item->setCheckState(Qt::Unchecked);
    }
    item->setText(text);
  }

  applyFilter();
  updateControls();
}  // TessdataDownloadDialog::fillList

void TessdataDownloadDialog::applyFilter() {
  const QString filter = m_filterEdit->text().trimmed();
  for (int row = 0; row < m_list->count(); ++row) {
    QListWidgetItem* item = m_list->item(row);
    if (item->data(Qt::UserRole).toInt() == kHeaderItem) {
      item->setHidden(!filter.isEmpty());
    } else {
      item->setHidden(!filter.isEmpty() && !item->text().contains(filter, Qt::CaseInsensitive));
    }
  }
}

void TessdataDownloadDialog::startDownload() {
  if (m_running || m_targetDir.isEmpty()) {
    return;
  }
  std::vector<TessdataFile> files;
  for (int row = 0; row < m_list->count(); ++row) {
    const QListWidgetItem* item = m_list->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      files.push_back(m_files[item->data(Qt::UserRole).toInt()]);
    }
  }
  if (files.empty()) {
    return;
  }
  m_progressBar->setValue(0);
  m_statusLabel->setText(tr("Downloading ..."));
  setRunning(true);
  m_downloader.download(files, m_targetDir);
}

void TessdataDownloadDialog::downloadProgress(const qint64 bytesDone, const qint64 bytesTotal) {
  if (bytesTotal <= 0) {
    return;
  }
  m_progressBar->setValue(static_cast<int>(bytesDone * 1000 / bytesTotal));
  const QLocale locale;
  m_statusLabel->setText(
      tr("Downloading ... %1 of %2").arg(locale.formattedDataSize(bytesDone), locale.formattedDataSize(bytesTotal)));
}

void TessdataDownloadDialog::downloadFinished(const QStringList& downloaded,
                                              const QString& error,
                                              const bool cancelled) {
  m_downloaded.append(downloaded);
  setRunning(false);
  m_progressBar->setValue(0);
  // Marks the new files as installed.
  fillList();

  QString status = tr("%n language(s) downloaded.", "", static_cast<int>(downloaded.size()));
  if (cancelled) {
    status = tr("Cancelled.") + ' ' + status;
  } else if (!error.isEmpty()) {
    status = error + ' ' + status;
    QMessageBox::warning(this, windowTitle(), error);
  }
  m_statusLabel->setText(status);
}

void TessdataDownloadDialog::updateControls() {
  bool anyChecked = false;
  for (int row = 0; row < m_list->count(); ++row) {
    const QListWidgetItem* item = m_list->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      anyChecked = true;
      break;
    }
  }
  m_downloadButton->setEnabled(!m_running && !m_targetDir.isEmpty() && anyChecked);
  m_closeButton->setText(m_running ? tr("Cancel") : tr("Close"));
}

void TessdataDownloadDialog::setRunning(const bool running) {
  m_running = running;
  m_list->setEnabled(!running);
  m_filterEdit->setEnabled(!running);
  updateControls();
}
