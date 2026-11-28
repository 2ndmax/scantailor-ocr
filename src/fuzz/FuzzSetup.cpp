// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FuzzSetup.h"

#include <QCoreApplication>
#include <QImageReader>

namespace {
void dropMessage(QtMsgType, const QMessageLogContext&, const QString&) {}
}  // namespace

extern "C" int LLVMFuzzerInitialize(int* argc, char*** argv) {
  // Lives until the process ends; libFuzzer never returns from its main loop.
  static QCoreApplication* const app = new QCoreApplication(*argc, *argv);
  (void) app;
  // Cap Qt image allocation at 256 MB (well below 10-fuzz.ps1's -RssLimitMb 2048 and ample for
  // MAX_FUZZ_PIXELS = 16 MP).
  QImageReader::setAllocationLimit(256);
  qInstallMessageHandler(&dropMessage);
  return 0;
}
