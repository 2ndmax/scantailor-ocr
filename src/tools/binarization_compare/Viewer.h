// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_VIEWER_H_
#define SCANTAILOR_TOOLS_VIEWER_H_

class QDir;

namespace binarization_compare {
/**
 * Writes index.html and data.js into outDir, listing every page folder that contains an
 * info.json, so pages from earlier runs stay in the comparison.
 */
bool writeViewer(const QDir& outDir);
}  // namespace binarization_compare
#endif  // SCANTAILOR_TOOLS_VIEWER_H_
