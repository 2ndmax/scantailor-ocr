// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FUZZ_FUZZSETUP_H_
#define SCANTAILOR_FUZZ_FUZZSETUP_H_

#include <QByteArray>
#include <cstddef>
#include <cstdint>

/*
 * Shared by all fuzz targets. FuzzSetup.cpp defines LLVMFuzzerInitialize(), which sets up what
 * the program's main() would: the application object and the allocation limit for Qt's image
 * readers. Qt's messages are dropped, as damaged input produces a warning in nearly every run.
 *
 * Each target defines LLVMFuzzerTestOneInput().
 */

namespace fuzz {
/**
 * The input as a QByteArray without a copy, so that AddressSanitizer reports reading past the
 * end of the input itself, not past a copy with spare capacity.
 */
inline QByteArray bytes(const uint8_t* data, const size_t size) {
  return QByteArray::fromRawData(reinterpret_cast<const char*>(data), static_cast<qsizetype>(size));
}
}  // namespace fuzz

#endif  // ifndef SCANTAILOR_FUZZ_FUZZSETUP_H_
