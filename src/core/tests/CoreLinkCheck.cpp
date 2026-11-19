// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// Does nothing. The build links every object file of the libraries without widgets into this program
// (see CMakeLists.txt); a link error means code there needs core_gui, a <step>_ui library or Qt Widgets.
int main() {
  return 0;
}
