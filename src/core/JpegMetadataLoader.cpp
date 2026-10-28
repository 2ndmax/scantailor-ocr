// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "JpegMetadataLoader.h"

#include <QDebug>
#include <QIODevice>
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstring>

#include "Dpm.h"
#include "ImageLoadErrors.h"
#include "ImageMetadata.h"
#include "NonCopyable.h"

extern "C" {
#include <jpeglib.h>
}

namespace {
/*======================== JpegDecompressionHandle =======================*/

class JpegDecompressHandle {
  DECLARE_NON_COPYABLE(JpegDecompressHandle)

 public:
  explicit JpegDecompressHandle(jpeg_error_mgr* errMgr);

  ~JpegDecompressHandle();

  /**
   * \brief Creates the underlying libjpeg object and installs \p srcMgr.
   *
   * This is deliberately not done by the constructor: jpeg_create_decompress()
   * may invoke the error handler, which longjmp()s. It therefore must only be
   * called once setjmp() has been set up, while the object itself has to be
   * constructed before that, so that its destructor still runs.
   */
  void create(jpeg_source_mgr* srcMgr);

  jpeg_decompress_struct* ptr() { return &m_info; }

  jpeg_decompress_struct* operator->() { return &m_info; }

 private:
  jpeg_decompress_struct m_info{};
};


JpegDecompressHandle::JpegDecompressHandle(jpeg_error_mgr* errMgr) {
  // m_info is value-initialized, so m_info.mem is null, which makes
  // jpeg_destroy_decompress() a safe no-op even if create() was never called.
  m_info.err = errMgr;
}

void JpegDecompressHandle::create(jpeg_source_mgr* srcMgr) {
  jpeg_create_decompress(&m_info);
  // jpeg_create_decompress() clears the source manager, so it has to be
  // installed afterwards.
  m_info.src = srcMgr;
}

JpegDecompressHandle::~JpegDecompressHandle() {
  jpeg_destroy_decompress(&m_info);
}

/*============================ JpegSourceManager =========================*/

class JpegSourceManager : public jpeg_source_mgr {
  DECLARE_NON_COPYABLE(JpegSourceManager)

 public:
  explicit JpegSourceManager(QIODevice& ioDevice);

 private:
  static void initSource(j_decompress_ptr cinfo);

  static boolean fillInputBuffer(j_decompress_ptr cinfo);

  boolean fillInputBufferImpl();

  static void skipInputData(j_decompress_ptr cinfo, long numBytes);

  void skipInputDataImpl(long numBytes);

  static void termSource(j_decompress_ptr cinfo);

  static JpegSourceManager* object(j_decompress_ptr cinfo);

  QIODevice& m_device;
  JOCTET m_buf[4096]{};
};


JpegSourceManager::JpegSourceManager(QIODevice& ioDevice) : jpeg_source_mgr(), m_device(ioDevice) {
  init_source = &JpegSourceManager::initSource;
  fill_input_buffer = &JpegSourceManager::fillInputBuffer;
  skip_input_data = &JpegSourceManager::skipInputData;
  resync_to_restart = &jpeg_resync_to_restart;
  term_source = &JpegSourceManager::termSource;
  bytes_in_buffer = 0;
  next_input_byte = m_buf;
}

void JpegSourceManager::initSource(j_decompress_ptr /*cinfo*/) {
  // No-op.
}

boolean JpegSourceManager::fillInputBuffer(j_decompress_ptr cinfo) {
  return object(cinfo)->fillInputBufferImpl();
}

boolean JpegSourceManager::fillInputBufferImpl() {
  const qint64 bytesRead = m_device.read(reinterpret_cast<char*>(m_buf), sizeof(m_buf));
  if (bytesRead > 0) {
    bytes_in_buffer = bytesRead;
  } else {
    // Insert a fake EOI marker.
    m_buf[0] = 0xFF;
    m_buf[1] = JPEG_EOI;
    bytes_in_buffer = 2;
  }
  next_input_byte = m_buf;
  return 1;
}

void JpegSourceManager::skipInputData(j_decompress_ptr cinfo, long numBytes) {
  object(cinfo)->skipInputDataImpl(numBytes);
}

void JpegSourceManager::skipInputDataImpl(long numBytes) {
  if (numBytes <= 0) {
    return;
  }

  while (numBytes > (long) bytes_in_buffer) {
    numBytes -= (long) bytes_in_buffer;
    fillInputBufferImpl();
  }
  next_input_byte += numBytes;
  bytes_in_buffer -= numBytes;
}

void JpegSourceManager::termSource(j_decompress_ptr /*cinfo*/) {
  // No-op.
}

JpegSourceManager* JpegSourceManager::object(j_decompress_ptr cinfo) {
  return static_cast<JpegSourceManager*>(cinfo->src);
}

/*============================= JpegErrorManager ===========================*/

class JpegErrorManager : public jpeg_error_mgr {
  DECLARE_NON_COPYABLE(JpegErrorManager)

 public:
  JpegErrorManager();

  jmp_buf& jmpBuf() { return m_jmpBuf; }

 private:
  static void errorExit(j_common_ptr cinfo);

  static void outputMessage(j_common_ptr cinfo);

  static QString formatMessage(j_common_ptr cinfo);

  static JpegErrorManager* object(j_common_ptr cinfo);

  jmp_buf m_jmpBuf{};
};


JpegErrorManager::JpegErrorManager() : jpeg_error_mgr() {
  jpeg_std_error(this);
  error_exit = &JpegErrorManager::errorExit;
  // libjpeg prints its warnings to stderr, which is invisible in a GUI.
  output_message = &JpegErrorManager::outputMessage;
}

void JpegErrorManager::errorExit(j_common_ptr cinfo) {
  // Shown with the file that couldn't be loaded, as the messages of libtiff and OpenJPEG.
  ImageLoadErrorCapture::addError(formatMessage(cinfo));
  longjmp(object(cinfo)->jmpBuf(), 1);
}

void JpegErrorManager::outputMessage(j_common_ptr cinfo) {
  ImageLoadErrorCapture::addWarning(formatMessage(cinfo));
}

QString JpegErrorManager::formatMessage(j_common_ptr cinfo) {
  char buffer[JMSG_LENGTH_MAX];
  cinfo->err->format_message(cinfo, buffer);
  return QString::fromLocal8Bit(buffer);
}

JpegErrorManager* JpegErrorManager::object(j_common_ptr cinfo) {
  return static_cast<JpegErrorManager*>(cinfo->err);
}

/*============================= EXIF orientation ===========================*/

// Returns the EXIF Orientation tag (1..8) from a saved APP1 marker, or 0 if there is none.
int exifOrientation(const jpeg_saved_marker_ptr marker) {
  static const unsigned char exifHeader[] = {'E', 'x', 'i', 'f', 0, 0};
  if ((marker->marker != JPEG_APP0 + 1) || (marker->data_length < sizeof(exifHeader))
      || (memcmp(marker->data, exifHeader, sizeof(exifHeader)) != 0)) {
    return 0;
  }
  // The rest is a TIFF structure; its offsets count from the TIFF header.
  const unsigned char* const tiff = marker->data + sizeof(exifHeader);
  const unsigned length = marker->data_length - sizeof(exifHeader);
  if (length < 8) {
    return 0;
  }
  bool bigEndian;
  if ((tiff[0] == 'M') && (tiff[1] == 'M')) {
    bigEndian = true;
  } else if ((tiff[0] == 'I') && (tiff[1] == 'I')) {
    bigEndian = false;
  } else {
    return 0;
  }
  const auto read16 = [&](const unsigned offset) -> unsigned {
    return bigEndian ? ((tiff[offset] << 8) | tiff[offset + 1]) : ((tiff[offset + 1] << 8) | tiff[offset]);
  };
  const auto read32 = [&](const unsigned offset) -> uint32_t {
    return bigEndian ? ((uint32_t(read16(offset)) << 16) | read16(offset + 2))
                     : ((uint32_t(read16(offset + 2)) << 16) | read16(offset));
  };

  const uint32_t ifd0 = read32(4);
  // Compared as subtraction, so a huge offset from a damaged file can't overflow.
  if ((ifd0 > length) || (length - ifd0 < 2)) {
    return 0;
  }
  const unsigned entryCount = read16(ifd0);
  static const unsigned entrySize = 12;
  for (unsigned i = 0; i < entryCount; ++i) {
    const uint32_t entry = ifd0 + 2 + i * entrySize;
    if (length - ifd0 < 2 + (i + 1) * entrySize) {
      return 0;
    }
    static const unsigned orientationTag = 0x0112;
    static const unsigned typeShort = 3;
    if ((read16(entry) == orientationTag) && (read16(entry + 2) == typeShort)) {
      const unsigned orientation = read16(entry + 8);
      return ((orientation >= 1) && (orientation <= 8)) ? int(orientation) : 0;
    }
  }
  return 0;
}

int exifOrientation(j_decompress_ptr cinfo) {
  for (jpeg_saved_marker_ptr marker = cinfo->marker_list; marker != nullptr; marker = marker->next) {
    const int orientation = exifOrientation(marker);
    if (orientation != 0) {
      return orientation;
    }
  }
  return 0;
}
}  // namespace

/*============================= JpegMetadataLoader ==========================*/

ImageMetadataLoader::Status JpegMetadataLoader::loadMetadata(QIODevice& ioDevice,
                                                             const VirtualFunction<void, const ImageMetadata&>& out) {
  if (!ioDevice.isReadable()) {
    return GENERIC_ERROR;
  }

  static const unsigned char jpeg_signature[] = {0xff, 0xd8, 0xff};
  static const int sigSize = sizeof(jpeg_signature);

  unsigned char signature[sigSize];
  if (ioDevice.peek((char*) signature, sigSize) != sigSize) {
    return FORMAT_NOT_RECOGNIZED;
  }
  if (memcmp(jpeg_signature, signature, sigSize) != 0) {
    return FORMAT_NOT_RECOGNIZED;
  }

  // All three objects have to be constructed before setjmp(), or longjmp()
  // would skip their destructors - most importantly the one calling
  // jpeg_destroy_decompress(). None of these constructors can longjmp().
  JpegErrorManager errMgr;
  JpegSourceManager srcMgr(ioDevice);
  JpegDecompressHandle cinfo(&errMgr);

  // REVIEW-NOTE (2026-10): a libjpeg error longjmp()s back here -> destructors of objects in the
  // frames jumped over are skipped, and locals changed after setjmp() are indeterminate.
  // Not fixed because no such object exists: the frames jumped over (libjpeg, JpegSourceManager,
  // errorExit, whose QString dies before longjmp()) hold none, nothing with a destructor is created
  // here between setjmp() and the last libjpeg call, and cinfo is changed only through the pointer
  // handed to libjpeg, so it is read from memory by its destructor, as in libjpeg's example.c.
  if (setjmp(errMgr.jmpBuf())) {
    // Returning from longjmp().
    return GENERIC_ERROR;
  }

  cinfo.create(&srcMgr);
  // Keeps APP1 for its EXIF orientation.
  jpeg_save_markers(cinfo.ptr(), JPEG_APP0 + 1, 0xffff);

  const int headerStatus = jpeg_read_header(cinfo.ptr(), 0);
  if (headerStatus == JPEG_HEADER_TABLES_ONLY) {
    return NO_IMAGES;
  }

  // The other possible value is JPEG_SUSPENDED, but we never suspend it.
  assert(headerStatus == JPEG_HEADER_OK);

  // The size and the resolution are known from the header. jpeg_start_decompress() isn't
  // needed for them, and would take memory for the whole image with progressive files.

  QSize size(cinfo->image_width, cinfo->image_height);
  Dpi dpi;
  if (cinfo->density_unit == 1) {
    // Dots per inch.
    dpi = Dpi(cinfo->X_density, cinfo->Y_density);
  } else if (cinfo->density_unit == 2) {
    // Dots per centimeter.
    dpi = Dpm(cinfo->X_density * 100, cinfo->Y_density * 100);
  }

  // ImageLoader turns the image upright by its EXIF orientation. Orientations 5 to 8 turn it by
  // 90 degrees, so the metadata has to swap the axes, or the size of the loaded image won't match
  // and LoadFileTask takes the file for replaced.
  if (exifOrientation(cinfo.ptr()) >= 5) {
    size.transpose();
    dpi = Dpi(dpi.vertical(), dpi.horizontal());
  }

  out(ImageMetadata(size, dpi));
  return LOADED;
}  // JpegMetadataLoader::loadMetadata
