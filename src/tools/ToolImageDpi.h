// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_TOOLIMAGEDPI_H_
#define SCANTAILOR_TOOLS_TOOLIMAGEDPI_H_

#include <QImage>

#include "Dpi.h"
#include "Dpm.h"

namespace tools {
/** The resolution stored in the image; 0 in a direction without one. */
inline Dpi storedDpi(const QImage& image) {
  return Dpi(Dpm(image));
}

/** The resolution stored in the image, or 300 dpi when missing/null, for the methods to work with. */
inline Dpi dpiOrDefault(const QImage& image) {
  const Dpi dpi = storedDpi(image);
  return dpi.isNull() ? Dpi(300, 300) : dpi;
}
}  // namespace tools

#endif  // SCANTAILOR_TOOLS_TOOLIMAGEDPI_H_
