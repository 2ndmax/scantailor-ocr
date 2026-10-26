// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_IMAGEPROC_CLAMPTOBYTE_H_
#define SCANTAILOR_IMAGEPROC_CLAMPTOBYTE_H_

#include <algorithm>
#include <cstdint>

namespace imageproc {
/** Limits \p val to 0..255 and rounds it to the nearest byte value. */
inline uint8_t clampToByte(const float val) {
  return static_cast<uint8_t>(std::clamp(val, 0.0f, 255.0f) + 0.5f);
}
}  // namespace imageproc

#endif  // SCANTAILOR_IMAGEPROC_CLAMPTOBYTE_H_
