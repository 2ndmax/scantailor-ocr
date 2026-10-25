// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PartlyAddedFilesMessage.h"

#include <QMessageBox>
#include <QStringList>

#include "ImageFileInfo.h"

void PartlyAddedFilesMessage::showIfNeeded(QWidget* parent, const std::vector<ImageFileInfo>& files) {
  QStringList lines;
  for (const ImageFileInfo& file : files) {
    for (const QString& reason : file.partialReads()) {
      lines.push_back(QString(QChar(0x2022)) + QLatin1Char(' ') + file.fileInfo().fileName() + QStringLiteral(" – ")
                      + reason);
    }
  }
  if (lines.isEmpty()) {
    return;
  }

  auto* box = new QMessageBox(parent);
  box->setIcon(QMessageBox::Warning);
  box->setWindowTitle(tr("Files Only Partly Added"));
  box->setText(tr("These files were only partly added:"));
  box->setInformativeText(lines.join(QLatin1Char('\n')));
  box->setStandardButtons(QMessageBox::Ok);
  box->setWindowModality(Qt::NonModal);
  box->setAttribute(Qt::WA_DeleteOnClose);
  box->show();
}
