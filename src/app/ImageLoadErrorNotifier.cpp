// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageLoadErrorNotifier.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QWidget>
#include <algorithm>
#include <utility>

#include "ImageLoadErrors.h"

namespace {
// How many failures are named in the message itself.  The rest is in the details.
const int NUM_ENTRIES_SHOWN = 3;
// Failures arriving within this time are shown together.
const int COLLECT_DELAY_MS = 750;
}  // namespace

ImageLoadErrorNotifier::ImageLoadErrorNotifier(QWidget* parentWindow)
    : QObject(parentWindow), m_parentWindow(parentWindow), m_muted(false) {
  m_delayTimer.setSingleShot(true);
  m_delayTimer.setInterval(COLLECT_DELAY_MS);
  connect(&m_delayTimer, &QTimer::timeout, this, &ImageLoadErrorNotifier::showPending);

  // Failures are reported from worker threads, hence the queued connection.
  connect(&ImageLoadErrorReporter::instance(), &ImageLoadErrorReporter::imageLoadFailed, this,
          &ImageLoadErrorNotifier::onImageLoadFailed, Qt::QueuedConnection);
  connect(&ImageLoadErrorReporter::instance(), &ImageLoadErrorReporter::imageWriteFailed, this,
          &ImageLoadErrorNotifier::onImageWriteFailed, Qt::QueuedConnection);
  connect(&ImageLoadErrorReporter::instance(), &ImageLoadErrorReporter::pageProcessingFailed, this,
          &ImageLoadErrorNotifier::onPageProcessingFailed, Qt::QueuedConnection);
}

ImageLoadErrorNotifier::~ImageLoadErrorNotifier() {
  if (m_messageBox) {
    delete m_messageBox.data();
  }
}

void ImageLoadErrorNotifier::onImageLoadFailed(const QString& filePath, const int page, const QStringList& messages) {
  addEntry(Entry{filePath, page, messages, Kind::LOAD});
}

void ImageLoadErrorNotifier::onImageWriteFailed(const QString& filePath, const QStringList& messages) {
  addEntry(Entry{filePath, 0, messages, Kind::WRITE});
}

void ImageLoadErrorNotifier::onPageProcessingFailed(const QString& filePath,
                                                    const int page,
                                                    const QStringList& messages) {
  addEntry(Entry{filePath, page, messages, Kind::PROCESSING});
}

void ImageLoadErrorNotifier::addEntry(Entry entry) {
  if (m_muted) {
    return;
  }

  m_entries.push_back(std::move(entry));

  if (m_messageBox && m_messageBox->isVisible()) {
    updateMessageBox();
  } else if (!m_delayTimer.isActive()) {
    // Not restarted by further failures, so a steady stream of them
    // doesn't postpone the message indefinitely.
    m_delayTimer.start();
  }
}

void ImageLoadErrorNotifier::showPending() {
  if (m_muted || m_entries.empty()) {
    return;
  }

  if (!m_messageBox) {
    auto* box = new QMessageBox(m_parentWindow);
    box->setIcon(QMessageBox::Warning);
    box->setWindowTitle(tr("Problems with Image Files"));
    box->setStandardButtons(QMessageBox::Ok);
    box->setWindowModality(Qt::NonModal);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setCheckBox(new QCheckBox(tr("Don't show this message again in this session"), box));
    connect(box, &QMessageBox::finished, this, [this, box]() {
      if (box->checkBox() && box->checkBox()->isChecked()) {
        m_muted = true;
      }
      // Whatever the user has seen doesn't need to be shown again.
      m_entries.clear();
    });
    m_messageBox = box;
  }

  updateMessageBox();
  m_messageBox->show();
  m_messageBox->raise();
}

QString ImageLoadErrorNotifier::describeFile(const Entry& entry, const bool fullPath) {
  QString name;
  if (entry.filePath.isEmpty()) {
    name = tr("Unknown page");
  } else {
    name = fullPath ? QDir::toNativeSeparators(entry.filePath) : QFileInfo(entry.filePath).fileName();
  }
  if (entry.page > 0) {
    name += QLatin1Char(' ') + tr("(page %1)").arg(entry.page);
  }
  if (entry.kind == Kind::WRITE) {
    name += QLatin1Char(' ') + tr("(writing)");
  } else if (entry.kind == Kind::PROCESSING) {
    name += QLatin1Char(' ') + tr("(processing)");
  }
  return name;
}

void ImageLoadErrorNotifier::updateMessageBox() {
  if (!m_messageBox) {
    return;
  }

  const auto numEntries = static_cast<int>(m_entries.size());
  const auto countKind = [this](const Kind kind) {
    return static_cast<int>(
        std::count_if(m_entries.begin(), m_entries.end(), [kind](const Entry& entry) { return entry.kind == kind; }));
  };
  const int numLoadFailures = countKind(Kind::LOAD);
  const int numWriteFailures = countKind(Kind::WRITE);
  const int numProcessingFailures = countKind(Kind::PROCESSING);

  QStringList headline;
  if (numLoadFailures > 0) {
    headline.push_back(tr("%n image(s) could not be loaded.", "", numLoadFailures));
  }
  if (numWriteFailures > 0) {
    headline.push_back(tr("%n output file(s) could not be written.", "", numWriteFailures));
  }
  if (numProcessingFailures > 0) {
    headline.push_back(
        tr("%n page(s) could not be processed because of an unexpected error.", "", numProcessingFailures));
  }
  m_messageBox->setText(headline.join(QLatin1Char('\n')));

  QStringList summary;
  const int numShown = std::min(numEntries, NUM_ENTRIES_SHOWN);
  for (int i = 0; i < numShown; ++i) {
    const Entry& entry = m_entries[i];
    QString line = QString(QChar(0x2022)) + QLatin1Char(' ') + describeFile(entry, false);
    if (!entry.messages.isEmpty()) {
      line += QLatin1String(":\n    ") + entry.messages.front();
    }
    summary.push_back(line);
  }
  if (numEntries > numShown) {
    summary.push_back(tr("... and %n more (see details).", "", numEntries - numShown));
  }
  m_messageBox->setInformativeText(summary.join(QLatin1Char('\n')));

  QStringList details;
  for (const Entry& entry : m_entries) {
    QString block = describeFile(entry, true);
    for (const QString& message : entry.messages) {
      block += QLatin1String("\n    ") + message;
    }
    details.push_back(block);
  }
  m_messageBox->setDetailedText(details.join(QLatin1String("\n\n")));
}
