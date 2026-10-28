// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TiffDevice.h"

#include <QIODevice>
#include <cstdio>

namespace tiff_device {
namespace {
tsize_t deviceRead(thandle_t context, tdata_t data, tsize_t size) {
  auto* dev = (QIODevice*) context;
  return (tsize_t) dev->read(static_cast<char*>(data), size);
}

tsize_t deviceWrite(thandle_t context, tdata_t data, tsize_t size) {
  auto* dev = (QIODevice*) context;
  return (tsize_t) dev->write(static_cast<char*>(data), size);
}

tsize_t noTransfer(thandle_t /*context*/, tdata_t /*data*/, tsize_t /*size*/) {
  // Not implemented for this access.
  return 0;
}

toff_t deviceSeek(thandle_t context, toff_t offset, int whence) {
  auto* dev = (QIODevice*) context;

  switch (whence) {
    case SEEK_SET:
      dev->seek(offset);
      break;
    case SEEK_CUR:
      dev->seek(dev->pos() + offset);
      break;
    case SEEK_END:
      dev->seek(dev->size() + offset);
      break;
    default:
      break;
  }
  return dev->pos();
}

int deviceClose(thandle_t /*context*/) {
  // Deliberately a no-op. libtiff calls this from TIFFClose(), but the device belongs to the
  // caller, who merely lends it to us and closes it; a QSaveFile must not be closed at all,
  // but committed.
  return 0;
}

toff_t deviceSize(thandle_t context) {
  auto* dev = (QIODevice*) context;
  return dev->size();
}

int deviceMap(thandle_t /*context*/, tdata_t* /*base*/, toff_t* /*size*/) {
  // Not implemented.
  return 0;
}

void deviceUnmap(thandle_t /*context*/, tdata_t /*base*/, toff_t /*size*/) {
  // Not implemented.
}
}  // namespace

TIFF* open(QIODevice& device, const char* name, const char* mode, const Access access) {
  const bool reading = (access == Access::READ);
  return TIFFClientOpen(name, mode, &device, reading ? &deviceRead : &noTransfer, reading ? &noTransfer : &deviceWrite,
                        &deviceSeek, &deviceClose, &deviceSize, &deviceMap, &deviceUnmap);
}
}  // namespace tiff_device
