// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_VIEWER_H_
#define SCANTAILOR_TOOLS_VIEWER_H_

#include <QString>

class QDir;

namespace binarization_compare {
/** The "mode" a page's info.json carries, so that the viewer shows only pages of its comparison. */
QString viewerMode(bool colors);

/**
 * Writes index.html and data.js into outDir, listing every page folder whose info.json belongs
 * to this comparison, so pages from earlier runs stay in the comparison.
 */
/** With colors set, the page compares the color reduction of colored text instead of binarization. */
bool writeViewer(const QDir& outDir, bool colors = false);
}  // namespace binarization_compare
#endif  // SCANTAILOR_TOOLS_VIEWER_H_
