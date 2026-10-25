// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PARTLYADDEDFILESMESSAGE_H_
#define SCANTAILOR_APP_PARTLYADDEDFILESMESSAGE_H_

#include <QCoreApplication>
#include <vector>

class ImageFileInfo;
class QWidget;

/**
 * \brief Tells the user which of the files just added could only be read in part
 *        (see ImageFileInfo::partialReads()), e.g. a multi-page TIFF with damaged pages.
 *
 * Used by every way of adding files: the "Project Files" dialog, inserting files and
 * the automatic import.
 */
class PartlyAddedFilesMessage {
  Q_DECLARE_TR_FUNCTIONS(PartlyAddedFilesMessage)

 public:
  PartlyAddedFilesMessage() = delete;

  /**
   * Shows one message for all files of \p files with partial reads, or nothing if there are
   * none. The message doesn't block, so that adding further files (e.g. new scans) goes on.
   */
  static void showIfNeeded(QWidget* parent, const std::vector<ImageFileInfo>& files);
};

#endif  // SCANTAILOR_APP_PARTLYADDEDFILESMESSAGE_H_
