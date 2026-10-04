#!/usr/bin/env bash
# Build ScanTailor OCR and create a .deb package for Ubuntu/Debian.
# Usage: ./build-deb.sh [build_dir]
# The .deb will be created in the project root.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Optional: custom build directory (default: build)
BUILD_DIR="${1:-build}"
mkdir -p "$BUILD_DIR"
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"
PKG_DIR="${BUILD_DIR}/debian-pkg"
DEBIAN_DIR="${PKG_DIR}/DEBIAN"

# Extract version from version.h.in
VERSION=$(sed -n 's/^#define VERSION "\([^"]*\)".*/\1/p' version.h.in)
if [[ -z "$VERSION" ]]; then
  echo "Error: could not read VERSION from version.h.in" >&2
  exit 1
fi

echo "Building ScanTailor OCR ${VERSION}"

# Configure and build
cd "$BUILD_DIR"
cmake -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_BUILD_TYPE=Release ..
make -j"$(nproc)"

# Install into staging directory for the .deb
rm -rf "$PKG_DIR"
make install DESTDIR="${PKG_DIR}"

# Debian control file
mkdir -p "$DEBIAN_DIR"
ARCH=$(dpkg --print-architecture 2>/dev/null || echo "amd64")

# Generate Depends via dpkg-shlibdeps when available (more accurate)
DEPS=""
if command -v dpkg-shlibdeps >/dev/null 2>&1; then
  SHLIBS_OUT=$(cd "${PKG_DIR}" && dpkg-shlibdeps -e usr/bin/scantailor-ocr -O 2>/dev/null || true)
  if [[ -n "$SHLIBS_OUT" ]]; then
    DEPS="${SHLIBS_OUT#shlibs:Depends=}"
  fi
fi
if [[ -z "$DEPS" ]]; then
  # Fallback when dpkg-shlibdeps is unavailable (e.g. cross-build). Include common
  # libjpeg variants across Debian/Ubuntu (see issue #64 / Ubuntu 22.04 vs bookworm).
  # CMake prefers Qt 6 when it is installed.
  if grep -q '^Qt6_DIR:PATH=/' "${BUILD_DIR}/CMakeCache.txt" 2>/dev/null; then
    QT_DEPS="libqt6core6t64 | libqt6core6, libqt6gui6t64 | libqt6gui6, libqt6widgets6t64 | libqt6widgets6, libqt6svg6t64 | libqt6svg6, libqt6xml6t64 | libqt6xml6, libqt6network6t64 | libqt6network6, libqt6opengl6t64 | libqt6opengl6, libqt6openglwidgets6t64 | libqt6openglwidgets6"
  else
    QT_DEPS="libqt5core5t64 | libqt5core5a, libqt5gui5t64 | libqt5gui5, libqt5widgets5t64 | libqt5widgets5, libqt5svg5t64 | libqt5svg5, libqt5xml5t64 | libqt5xml5, libqt5network5t64 | libqt5network5"
  fi
  DEPS="libc6, libstdc++6, libgcc-s1, ${QT_DEPS}, libboost-filesystem1.83.0 | libboost-filesystem1.74.0, libjpeg62-turbo | libjpeg-turbo8 | libjpeg8, libpng16-16, libtiff6, libopenjp2-7, zlib1g, libtesseract5, liblept5 | libleptonica6"
fi

cat > "${DEBIAN_DIR}/control" << EOF
Package: scantailor-ocr
Version: ${VERSION}
Section: graphics
Priority: optional
Architecture: ${ARCH}
Depends: ${DEPS}
Recommends: tesseract-ocr-eng, tesseract-ocr-deu
Maintainer: ScanTailor OCR <https://github.com/2ndmax/scantailor-ocr>
Description: Scanned page post-processing with searchable PDF export (OCR)
 ScanTailor OCR is based on ScanTailor Advanced 1.2.1. It cleans up scanned
 pages (page splitting, deskewing, content selection, margins, dewarping)
 and exports them as a compact, searchable PDF: text recognition with
 Tesseract and lossless JBIG2 for black and white pages.
Homepage: https://github.com/2ndmax/scantailor-ocr
Bugs: https://github.com/2ndmax/scantailor-ocr/issues
EOF

# Optional: refresh icon and desktop caches after install
cat > "${DEBIAN_DIR}/postinst" << 'POSTINST'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database /usr/share/applications 2>/dev/null || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f -t /usr/share/icons/hicolor 2>/dev/null || true
fi
if command -v update-mime-database >/dev/null 2>&1; then
  update-mime-database /usr/share/mime 2>/dev/null || true
fi
POSTINST
chmod 755 "${DEBIAN_DIR}/postinst"

# Build the .deb
cd "$SCRIPT_DIR"
dpkg-deb --root-owner-group --build "$PKG_DIR" "scantailor-ocr_${VERSION}_${ARCH}.deb"

echo "Done: scantailor-ocr_${VERSION}_${ARCH}.deb"
