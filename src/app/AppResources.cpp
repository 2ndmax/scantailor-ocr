// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "AppResources.h"

#include <QtCore/QtGlobal>

void initAppResources() {
  // Static Qt resource registrations; keep in this translation unit so callers
  // only need to call one function.
  Q_INIT_RESOURCE(resources);
  Q_INIT_RESOURCE(dark_scheme);
  Q_INIT_RESOURCE(light_scheme);
}
