// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TESTS_TESTTIFFFILE_H_
#define SCANTAILOR_CORE_TESTS_TESTTIFFFILE_H_

#include <tiffio.h>

#include <QFile>
#include <QString>

namespace Tests {
/**
 * Opens a TIFF file with libtiff for the tests, which write or inspect files directly.
 *
 * On Windows, TIFFOpen() takes the path in the ANSI code page, so a temporary folder with
 * other characters (e.g. under a Cyrillic user name on a German Windows) would fail.
 */
inline TIFF* openTiffFile(const QString& path, const char* mode) {
#ifdef _WIN32
  return TIFFOpenW(reinterpret_cast<const wchar_t*>(path.utf16()), mode);
#else
  return TIFFOpen(QFile::encodeName(path).constData(), mode);
#endif
}
}  // namespace Tests

#endif  // SCANTAILOR_CORE_TESTS_TESTTIFFFILE_H_
