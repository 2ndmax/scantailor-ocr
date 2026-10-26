// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_SETTINGSSETUP_H_
#define SCANTAILOR_CORE_SETTINGSSETUP_H_

#include <QString>

/** Where the program keeps its settings and profiles. Needs a QCoreApplication, not a GUI. */
namespace settings_setup {
/**
 * The config folder of the portable version next to the program in \p programDir, created if
 * missing. A null string if the build isn't portable or the folder can't be written to; then the
 * user's usual folders are used.
 */
QString portableConfigPath(const QString& programDir);

/** portableConfigPath() for the running program. Checked once, the result is kept. */
QString portableConfigPath();

/**
 * Prepares QSettings for the program: its name, the INI format, the portable config folder if
 * there is one, the settings taken over from ScanTailor Advanced, and the number of processing
 * threads. Call it once at startup, before the settings are read.
 */
void setUp();
}  // namespace settings_setup

#endif
