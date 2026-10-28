// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_TOOLS_BENCH_H_
#define SCANTAILOR_TOOLS_BENCH_H_

#include <QString>

// Benchmark of the despeckling on generated pages whose truth is known: German text with small
// marks (dots above letters, punctuation, dot leaders, superscripts) and several kinds of noise,
// every pixel labelled. Despeckles each page at the three levels and counts, per class, how many
// noise components were removed and how many text components were lost.
//
// Pages: two fonts (serif, sans serif) at 6, 8, 10, 12 and 14 pt, rendered at 200, 300, 400 and 600 dpi.
// The fonts come from this machine, so compare results only between runs on the same machine.
//
// Writes bench.csv (one row per page and level) to 'outDir' and prints a summary. With 'images',
// also writes, per page, the result at NORMAL in colour (black = text kept, blue = text lost,
// red = noise kept, light gray = noise removed) and per level a sheet of crops around lost text.
int bench(const QString& outDir, bool images);

#endif  // SCANTAILOR_TOOLS_BENCH_H_
