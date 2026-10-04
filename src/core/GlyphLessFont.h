// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_GLYPHLESSFONT_H_
#define SCANTAILOR_CORE_GLYPHLESSFONT_H_

#include <QByteArray>

/**
 * \brief The TrueType file of Tesseract's "GlyphLessFont".
 *
 * Its only glyph is invisible and 500 units (half an em) wide.  PdfWriter maps every
 * character to it, which gives an invisible but searchable and selectable text layer.
 */
QByteArray glyphLessFontData();

#endif  // SCANTAILOR_CORE_GLYPHLESSFONT_H_
