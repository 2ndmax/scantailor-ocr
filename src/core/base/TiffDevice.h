// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TIFFDEVICE_H_
#define SCANTAILOR_CORE_TIFFDEVICE_H_

#include <tiffio.h>

class QIODevice;

/** The link between libtiff and a QIODevice, shared by TiffReader and TiffWriter. */
namespace tiff_device {
/** Owns an open TIFF and closes it. */
class TiffHandle {
 public:
  explicit TiffHandle(TIFF* handle) : m_handle(handle) {}

  ~TiffHandle() {
    if (m_handle) {
      TIFFClose(m_handle);
    }
  }

  TiffHandle(const TiffHandle&) = delete;

  TiffHandle& operator=(const TiffHandle&) = delete;

  TIFF* handle() const { return m_handle; }

 private:
  TIFF* m_handle;
};

enum class Access { READ, WRITE };

/**
 * \brief Opens a TIFF on \p device, which stays open and owned by the caller.
 *
 * Only the given access is passed through to the device; the other direction does nothing.
 *
 * \param name The name libtiff uses in its messages.
 * \param mode The libtiff mode, such as "rBm".
 * \return The TIFF, or null on failure.
 */
TIFF* open(QIODevice& device, const char* name, const char* mode, Access access);
}  // namespace tiff_device

#endif  // ifndef SCANTAILOR_CORE_TIFFDEVICE_H_
