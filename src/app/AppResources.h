// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_APPRESOURCES_H_
#define SCANTAILOR_APP_APPRESOURCES_H_

/**
 * Initializes the Qt resources compiled into the app library.
 *
 * Resources from a static library can be dropped by the linker unless a symbol
 * from their object file is referenced. Call this once before creating widgets
 * that need app icons or color schemes, both from the application and from tests
 * that link the app library.
 */
void initAppResources();

#endif  // SCANTAILOR_APP_APPRESOURCES_H_
