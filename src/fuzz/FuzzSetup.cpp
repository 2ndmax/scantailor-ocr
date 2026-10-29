// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FuzzSetup.h"

#include <QCoreApplication>
#include <QImageReader>

#include "ImageSizeLimit.h"

namespace {
void dropMessage(QtMsgType, const QMessageLogContext&, const QString&) {}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // Lives until the process ends; libFuzzer never returns from its main loop.
  static QCoreApplication* const app = new QCoreApplication(*argc, *argv);
  (void) app;
  // As in the program's main().
  QImageReader::setAllocationLimit(static_cast<int>(ImageSizeLimit::MAX_PIXELS * 8 / (1024 * 1024)) + 1);
  qInstallMessageHandler(&dropMessage);
  return 0;
}
