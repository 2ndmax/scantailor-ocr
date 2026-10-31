// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoadErrors.h>
#include <ImageLoader.h>
#include <ImageMetadata.h>
#include <ImageMetadataLoader.h>
#include <ImageSizeLimit.h>
#include <Jp2Reader.h>
#include <TiffReader.h>
#include <TiffWriter.h>
#include <openjpeg.h>
#include <tiffio.h>

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <algorithm>
#include <boost/test/unit_test.hpp>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <vector>

extern "C" {
#include <jpeglib.h>
}

#include "TestHelpers.h"
#include "TestTiffFile.h"

namespace {
// Describes a single-page TIFF to be written by writeTiff().
struct TiffSpec {
  int width = 0;
  int height = 0;
  uint16_t bitsPerSample = 8;
  uint16_t samplesPerPixel = 1;
  uint16_t sampleFormat = SAMPLEFORMAT_UINT;
  uint16_t photometric = PHOTOMETRIC_MINISBLACK;
  uint16_t compression = COMPRESSION_NONE;
  uint16_t planarConfig = PLANARCONFIG_CONTIG;
  uint32_t tileSize = 0;  // 0 means stripped.
  uint32_t rowsPerStrip = 8;
  bool bigEndian = false;
  std::vector<uint16_t> colormap;  // 3 * 2^bitsPerSample entries for palette images.
  int extraSample = -1;            // The kind of the last sample (EXTRASAMPLE_*), -1 for none.
  uint16_t resolutionUnit = RESUNIT_INCH;
};

// Returns the value of one sample of the image to be written.
using SampleFunc = std::function<double(int x, int y, int sample)>;

// Stores a sample value at position \p index of a row, in the on-disk representation.
void packSample(const TiffSpec& spec, const double value, uint8_t* row, const size_t index) {
  if (spec.bitsPerSample < 8) {
    const auto v = static_cast<unsigned>(value);
    const size_t bit = index * spec.bitsPerSample;
    const int shift = 8 - spec.bitsPerSample - static_cast<int>(bit % 8);
    row[bit / 8] |= static_cast<uint8_t>(v << shift);
    return;
  }
  uint8_t* dst = row + index * (spec.bitsPerSample / 8);
  if (spec.sampleFormat == SAMPLEFORMAT_IEEEFP) {
    if (spec.bitsPerSample == 32) {
      const auto f = static_cast<float>(value);
      std::memcpy(dst, &f, sizeof(f));
    } else {
      std::memcpy(dst, &value, sizeof(value));
    }
  } else if (spec.sampleFormat == SAMPLEFORMAT_INT) {
    if (spec.bitsPerSample == 16) {
      const auto v = static_cast<int16_t>(value);
      std::memcpy(dst, &v, sizeof(v));
    } else {
      const auto v = static_cast<int32_t>(value);
      std::memcpy(dst, &v, sizeof(v));
    }
  } else {
    if (spec.bitsPerSample == 8) {
      *dst = static_cast<uint8_t>(value);
    } else if (spec.bitsPerSample == 16) {
      const auto v = static_cast<uint16_t>(value);
      std::memcpy(dst, &v, sizeof(v));
    } else {
      const auto v = static_cast<uint32_t>(value);
      std::memcpy(dst, &v, sizeof(v));
    }
  }
}

// Writes the tags and the data of the current directory.
bool writeTiffPage(TIFF* tif, const TiffSpec& spec, const SampleFunc& sample) {
  TIFFSetField(tif, TIFFTAG_IMAGEWIDTH, static_cast<uint32_t>(spec.width));
  TIFFSetField(tif, TIFFTAG_IMAGELENGTH, static_cast<uint32_t>(spec.height));
  TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE, spec.bitsPerSample);
  TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, spec.samplesPerPixel);
  TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT, spec.sampleFormat);
  TIFFSetField(tif, TIFFTAG_PHOTOMETRIC, spec.photometric);
  TIFFSetField(tif, TIFFTAG_COMPRESSION, spec.compression);
  TIFFSetField(tif, TIFFTAG_PLANARCONFIG, spec.planarConfig);
  TIFFSetField(tif, TIFFTAG_XRESOLUTION, 300.0f);
  TIFFSetField(tif, TIFFTAG_YRESOLUTION, 300.0f);
  TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, spec.resolutionUnit);
  if (!spec.colormap.empty()) {
    const size_t n = spec.colormap.size() / 3;
    TIFFSetField(tif, TIFFTAG_COLORMAP, spec.colormap.data(), spec.colormap.data() + n, spec.colormap.data() + 2 * n);
  }
  if (spec.extraSample >= 0) {
    const auto extra = static_cast<uint16_t>(spec.extraSample);
    TIFFSetField(tif, TIFFTAG_EXTRASAMPLES, uint16_t(1), &extra);
  }

  const bool separate = spec.planarConfig == PLANARCONFIG_SEPARATE;
  const int numPlanes = separate ? spec.samplesPerPixel : 1;
  const int samplesPerRowPixel = separate ? 1 : spec.samplesPerPixel;

  bool ok = true;
  if (spec.tileSize) {
    TIFFSetField(tif, TIFFTAG_TILEWIDTH, spec.tileSize);
    TIFFSetField(tif, TIFFTAG_TILELENGTH, spec.tileSize);
    const size_t tileRowBytes = (static_cast<size_t>(spec.tileSize) * samplesPerRowPixel * spec.bitsPerSample + 7) / 8;
    std::vector<uint8_t> tile(TIFFTileSize(tif));
    for (int p = 0; p < numPlanes && ok; ++p) {
      for (uint32_t y0 = 0; y0 < static_cast<uint32_t>(spec.height); y0 += spec.tileSize) {
        for (uint32_t x0 = 0; x0 < static_cast<uint32_t>(spec.width); x0 += spec.tileSize) {
          std::fill(tile.begin(), tile.end(), 0);
          for (uint32_t ty = 0; ty < spec.tileSize; ++ty) {
            for (uint32_t tx = 0; tx < spec.tileSize; ++tx) {
              const int x = static_cast<int>(x0 + tx);
              const int y = static_cast<int>(y0 + ty);
              if ((x >= spec.width) || (y >= spec.height)) {
                continue;
              }
              for (int s = 0; s < samplesPerRowPixel; ++s) {
                const int sampleIdx = separate ? p : s;
                packSample(spec, sample(x, y, sampleIdx), tile.data() + ty * tileRowBytes, tx * samplesPerRowPixel + s);
              }
            }
          }
          if (TIFFWriteTile(tif, tile.data(), x0, y0, 0, static_cast<uint16_t>(p)) < 0) {
            ok = false;
          }
        }
      }
    }
  } else {
    TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP, spec.rowsPerStrip);
    std::vector<uint8_t> row(TIFFScanlineSize(tif));
    for (int p = 0; p < numPlanes && ok; ++p) {
      for (int y = 0; y < spec.height && ok; ++y) {
        std::fill(row.begin(), row.end(), 0);
        for (int x = 0; x < spec.width; ++x) {
          for (int s = 0; s < samplesPerRowPixel; ++s) {
            packSample(spec, sample(x, y, separate ? p : s), row.data(),
                       static_cast<size_t>(x) * samplesPerRowPixel + s);
          }
        }
        if (TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(y), static_cast<uint16_t>(p)) < 0) {
          ok = false;
        }
      }
    }
  }
  return ok;
}

// Writes \p numPages identical pages.
bool writeTiff(const QString& path, const TiffSpec& spec, const SampleFunc& sample, const int numPages = 1) {
  TIFF* tif = Tests::openTiffFile(path, spec.bigEndian ? "wb" : "wl");
  if (!tif) {
    return false;
  }
  bool ok = true;
  for (int page = 0; (page < numPages) && ok; ++page) {
    ok = writeTiffPage(tif, spec, sample) && TIFFWriteDirectory(tif);
  }
  TIFFClose(tif);
  return ok;
}

/*
 * Little-endian TIFF files only: helpers that damage the file in a controlled way.
 */
uint32_t readLe(const QByteArray& data, const uint32_t offset, const int bytes) {
  uint32_t value = 0;
  for (int i = bytes - 1; i >= 0; --i) {
    value = (value << 8) | static_cast<uint8_t>(data[offset + i]);
  }
  return value;
}

void writeLe(QByteArray& data, const uint32_t offset, const int bytes, uint32_t value) {
  for (int i = 0; i < bytes; ++i, value >>= 8) {
    data[offset + i] = static_cast<char>(value & 0xff);
  }
}

// The offset of the directory of page \p page.
uint32_t ifdOffset(const QByteArray& data, const int page) {
  uint32_t offset = readLe(data, 4, 4);
  for (int i = 0; i < page; ++i) {
    const uint32_t entryCount = readLe(data, offset, 2);
    offset = readLe(data, offset + 2 + 12 * entryCount, 4);
  }
  return offset;
}

// Replaces the value of \p tag in the directory at \p ifd by a single LONG.
bool setTiffTag(QByteArray& data, const uint32_t ifd, const uint16_t tag, const uint32_t value) {
  const uint32_t entryCount = readLe(data, ifd, 2);
  for (uint32_t i = 0; i < entryCount; ++i) {
    const uint32_t entry = ifd + 2 + 12 * i;
    if (readLe(data, entry, 2) == tag) {
      writeLe(data, entry + 2, 2, TIFF_LONG);
      writeLe(data, entry + 4, 4, 1);
      writeLe(data, entry + 8, 4, value);
      return true;
    }
  }
  return false;
}

// Makes the first directory of a small file claim \p size. Its one strip then holds all rows, as
// in a real file with one strip: with the rows per strip of the small image, libtiff 4.5 (Ubuntu
// 24.04) refuses the directory, as the file is far too small for that many strips.
bool claimTiffSize(QByteArray& data, const QSize& size) {
  const uint32_t ifd = ifdOffset(data, 0);
  return setTiffTag(data, ifd, TIFFTAG_IMAGEWIDTH, size.width())
         && setTiffTag(data, ifd, TIFFTAG_IMAGELENGTH, size.height())
         && setTiffTag(data, ifd, TIFFTAG_ROWSPERSTRIP, size.height());
}

QStringList readTiffMetadata(const QString& path,
                             std::vector<ImageMetadata>& pages,
                             ImageMetadataLoader::Status& status) {
  ImageLoadErrorCapture capture;
  status = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { pages.push_back(metadata); });
  return capture.messages();
}

QImage readTiff(const QString& path, QStringList* messages = nullptr) {
  ImageLoadErrorCapture capture;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return QImage();
  }
  QImage image = TiffReader::readImage(file, 0);
  if (messages) {
    *messages = capture.messages();
  }
  return image;
}

int grayAt(const QImage& image, const int x, const int y) {
  return qGray(image.pixel(x, y));
}

// tileSize 0 writes a single tile. imageOffset moves the image on the reference grid; the tile
// grid stays at 0.
bool writeJp2(const QString& path,
              const int width,
              const int height,
              const int numComps,
              const bool codestream,
              const int tileSize = 0,
              const int imageOffset = 0) {
  std::vector<opj_image_cmptparm_t> params(numComps);
  for (auto& p : params) {
    std::memset(&p, 0, sizeof(p));
    p.dx = 1;
    p.dy = 1;
    p.x0 = imageOffset;
    p.y0 = imageOffset;
    p.w = width;
    p.h = height;
    p.prec = 8;
    p.sgnd = 0;
  }
  opj_image_t* image = opj_image_create(numComps, params.data(), numComps == 1 ? OPJ_CLRSPC_GRAY : OPJ_CLRSPC_SRGB);
  if (!image) {
    return false;
  }
  image->x0 = imageOffset;
  image->y0 = imageOffset;
  image->x1 = imageOffset + width;
  image->y1 = imageOffset + height;
  for (int c = 0; c < numComps; ++c) {
    for (int y = 0; y < height; ++y) {
      for (int x = 0; x < width; ++x) {
        image->comps[c].data[y * width + x] = (x * 4 + y * 2 + c * 60) % 256;
      }
    }
  }

  opj_cparameters_t cparams;
  opj_set_default_encoder_parameters(&cparams);
  cparams.tcp_numlayers = 1;
  cparams.tcp_rates[0] = 0;  // Lossless.
  cparams.cp_disto_alloc = 1;
  cparams.numresolution = 4;
  if (tileSize > 0) {
    cparams.tile_size_on = OPJ_TRUE;
    cparams.cp_tdx = tileSize;
    cparams.cp_tdy = tileSize;
  }

  opj_codec_t* codec = opj_create_compress(codestream ? OPJ_CODEC_J2K : OPJ_CODEC_JP2);
  bool ok = codec && opj_setup_encoder(codec, &cparams, image);
  // REVIEW-NOTE (2026-10): OpenJPEG opens the file in the ANSI code page -> with a temporary folder
  // outside it (e.g. a Cyrillic user name on a German Windows) the JPEG 2000 tests fail. Not fixed
  // because only the test setup is affected (Jp2Reader reads through QIODevice) and a memory stream
  // here would copy the one in PdfImageEncoder.
  opj_stream_t* stream
      = ok ? opj_stream_create_default_file_stream(QFile::encodeName(path).constData(), OPJ_FALSE) : nullptr;
  ok = ok && stream && opj_start_compress(codec, image, stream) && opj_encode(codec, stream)
       && opj_end_compress(codec, stream);
  if (stream) {
    opj_stream_destroy(stream);
  }
  if (codec) {
    opj_destroy_codec(codec);
  }
  opj_image_destroy(image);
  return ok;
}

// Writes a gray JPEG; exifOrientation 0 writes no EXIF segment.
void writeGrayJpeg(const QString& path,
                   const int width,
                   const int height,
                   const int xDpi,
                   const int yDpi,
                   const bool progressive,
                   const int exifOrientation = 0) {
  jpeg_compress_struct cinfo{};
  jpeg_error_mgr jerr{};
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_compress(&cinfo);
  unsigned char* buffer = nullptr;
  unsigned long size = 0;
  jpeg_mem_dest(&cinfo, &buffer, &size);
  cinfo.image_width = width;
  cinfo.image_height = height;
  cinfo.input_components = 1;
  cinfo.in_color_space = JCS_GRAYSCALE;
  jpeg_set_defaults(&cinfo);
  if (progressive) {
    jpeg_simple_progression(&cinfo);
  }
  cinfo.density_unit = 1;  // dpi
  cinfo.X_density = static_cast<UINT16>(xDpi);
  cinfo.Y_density = static_cast<UINT16>(yDpi);
  jpeg_start_compress(&cinfo, TRUE);
  if (exifOrientation != 0) {
    // "Exif\0\0", a big-endian TIFF header and an IFD0 holding only Orientation (0x0112, SHORT).
    const unsigned char exif[]
        = {'E', 'x', 'i', 'f',  0,    0, 'M', 'M', 0, 0x2a, 0, 0, 0,
           8,   0,   1,   0x01, 0x12, 0, 3,   0,   0, 0,    1, 0, static_cast<unsigned char>(exifOrientation),
           0,   0,   0,   0,    0,    0};
    jpeg_write_marker(&cinfo, JPEG_APP0 + 1, exif, sizeof(exif));
  }
  std::vector<JSAMPLE> row(width, 128);
  while (cinfo.next_scanline < cinfo.image_height) {
    JSAMPROW rowPtr = row.data();
    jpeg_write_scanlines(&cinfo, &rowPtr, 1);
  }
  jpeg_finish_compress(&cinfo);
  jpeg_destroy_compress(&cinfo);
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::WriteOnly));
  file.write(reinterpret_cast<const char*>(buffer), static_cast<qint64>(size));
  free(buffer);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(CoreImageReadersTestSuite)

BOOST_AUTO_TEST_CASE(test_tiled_bilevel_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("tiled_bw.tif");
  TiffSpec spec;
  spec.width = 100;
  spec.height = 70;
  spec.bitsPerSample = 1;
  spec.photometric = PHOTOMETRIC_MINISWHITE;
  spec.compression = COMPRESSION_CCITTFAX4;
  spec.tileSize = 32;
  auto black = [](int x, int y) { return (x + y) % 3 == 0; };
  BOOST_REQUIRE(writeTiff(path, spec, [&](int x, int y, int) { return black(x, y) ? 1.0 : 0.0; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK(image.format() == QImage::Format_Mono);
  BOOST_CHECK(image.size() == QSize(100, 70));
  bool allMatch = true;
  for (int y = 0; y < spec.height; ++y) {
    for (int x = 0; x < spec.width; ++x) {
      allMatch = allMatch && ((grayAt(image, x, y) == 0) == black(x, y));
    }
  }
  BOOST_CHECK(allMatch);
}

BOOST_AUTO_TEST_CASE(test_tiled_jpeg_gray_tiff) {
  if (!TIFFIsCODECConfigured(COMPRESSION_JPEG)) {
    BOOST_TEST_MESSAGE("libtiff has no JPEG support, skipped");
    return;
  }
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("tiled_jpeg.tif");
  TiffSpec spec;
  spec.width = 80;
  spec.height = 50;
  spec.compression = COMPRESSION_JPEG;
  spec.tileSize = 16;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int, int) { return x < 40 ? 30.0 : 220.0; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  // Lossy, so only roughly.
  BOOST_CHECK(std::abs(grayAt(image, 10, 25) - 30) < 12);
  BOOST_CHECK(std::abs(grayAt(image, 70, 25) - 220) < 12);
}

BOOST_AUTO_TEST_CASE(test_4bit_gray_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("gray4.tif");
  TiffSpec spec;
  spec.width = 37;
  spec.height = 9;
  spec.bitsPerSample = 4;
  spec.compression = COMPRESSION_LZW;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int, int) { return x % 16; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  bool allMatch = true;
  for (int x = 0; x < spec.width; ++x) {
    allMatch = allMatch && (grayAt(image, x, 4) == (x % 16) * 17);
  }
  BOOST_CHECK(allMatch);
}

BOOST_AUTO_TEST_CASE(test_big_endian_palette_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("palette_be.tif");
  TiffSpec spec;
  spec.width = 16;
  spec.height = 4;
  spec.photometric = PHOTOMETRIC_PALETTE;
  spec.bigEndian = true;
  spec.colormap.resize(3 * 256);
  for (int i = 0; i < 256; ++i) {
    spec.colormap[i] = static_cast<uint16_t>(i * 257);                // red
    spec.colormap[256 + i] = 0;                                       // green
    spec.colormap[512 + i] = static_cast<uint16_t>((255 - i) * 257);  // blue
  }
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int, int) { return x * 16; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  const QRgb px = image.pixel(3, 1);
  BOOST_CHECK_EQUAL(qRed(px), 48);
  BOOST_CHECK_EQUAL(qGreen(px), 0);
  BOOST_CHECK_EQUAL(qBlue(px), 255 - 48);
}

BOOST_AUTO_TEST_CASE(test_float_gray_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("float.tif");
  TiffSpec spec;
  spec.width = 256;
  spec.height = 3;
  spec.bitsPerSample = 32;
  spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
  spec.compression = COMPRESSION_ADOBE_DEFLATE;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int, int) { return x / 255.0; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(grayAt(image, 0, 1), 0);
  BOOST_CHECK_EQUAL(grayAt(image, 128, 1), 128);
  BOOST_CHECK_EQUAL(grayAt(image, 255, 1), 255);
}

BOOST_AUTO_TEST_CASE(test_signed_int_gray_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("int16.tif");
  TiffSpec spec;
  spec.width = 11;
  spec.height = 2;
  spec.bitsPerSample = 16;
  spec.sampleFormat = SAMPLEFORMAT_INT;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int, int) { return (x - 5) * 200.0; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK_EQUAL(grayAt(image, 0, 0), 0);
  BOOST_CHECK_EQUAL(grayAt(image, 10, 0), 255);
}

BOOST_AUTO_TEST_CASE(test_float_rgb_separate_planes_tiff) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("float_rgb.tif");
  TiffSpec spec;
  spec.width = 20;
  spec.height = 10;
  spec.bitsPerSample = 32;
  spec.samplesPerPixel = 3;
  spec.sampleFormat = SAMPLEFORMAT_IEEEFP;
  spec.photometric = PHOTOMETRIC_RGB;
  spec.planarConfig = PLANARCONFIG_SEPARATE;
  spec.tileSize = 16;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int s) { return s == 0 ? 1.0 : (s == 1 ? 0.5 : 0.0); }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  const QRgb px = image.pixel(17, 9);
  BOOST_CHECK_EQUAL(qRed(px), 255);
  BOOST_CHECK_EQUAL(qGreen(px), 128);
  BOOST_CHECK_EQUAL(qBlue(px), 0);
}

BOOST_AUTO_TEST_CASE(test_unassociated_alpha_tiff) {
  // libtiff premultiplies unassociated alpha; the image must say so.
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("rgba.tif");
  TiffSpec spec;
  spec.width = 20;
  spec.height = 10;
  spec.samplesPerPixel = 4;
  spec.photometric = PHOTOMETRIC_RGB;
  spec.extraSample = EXTRASAMPLE_UNASSALPHA;
  const int rgba[] = {200, 100, 50, 128};
  BOOST_REQUIRE(writeTiff(path, spec, [&rgba](int, int, int s) { return rgba[s]; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  const QRgb px = image.convertToFormat(QImage::Format_ARGB32).pixel(7, 3);
  BOOST_CHECK_EQUAL(qAlpha(px), 128);
  BOOST_CHECK_LE(std::abs(qRed(px) - 200), 2);
  BOOST_CHECK_LE(std::abs(qGreen(px) - 100), 2);
  BOOST_CHECK_LE(std::abs(qBlue(px) - 50), 2);
}

BOOST_AUTO_TEST_CASE(test_lzma_and_zstd_tiff) {
  for (const uint16_t compression : {uint16_t(COMPRESSION_LZMA), uint16_t(COMPRESSION_ZSTD)}) {
    if (!TIFFIsCODECConfigured(compression)) {
      BOOST_TEST_MESSAGE("libtiff lacks codec " << compression << ", skipped");
      continue;
    }
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString path = dir.filePath("compressed.tif");
    TiffSpec spec;
    spec.width = 40;
    spec.height = 30;
    spec.compression = compression;
    BOOST_REQUIRE(writeTiff(path, spec, [](int x, int y, int) { return (x * y) % 256; }));

    const QImage image = readTiff(path);
    BOOST_REQUIRE(!image.isNull());
    BOOST_CHECK_EQUAL(grayAt(image, 7, 9), 63);
  }
}

BOOST_AUTO_TEST_CASE(test_tiff_metadata) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("meta.tif");
  TiffSpec spec;
  spec.width = 33;
  spec.height = 44;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));

  std::vector<ImageMetadata> pages;
  const auto status
      = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { pages.push_back(metadata); });
  BOOST_REQUIRE(status == ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(pages.size(), 1u);
  BOOST_CHECK(pages[0].size() == QSize(33, 44));
  BOOST_CHECK_EQUAL(pages[0].dpi().horizontal(), 300);
}

BOOST_AUTO_TEST_CASE(test_progressive_jpeg_metadata) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("progressive.jpg");
  writeGrayJpeg(path, 64, 48, 300, 300, true);

  std::vector<ImageMetadata> pages;
  const auto status
      = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { pages.push_back(metadata); });
  BOOST_REQUIRE(status == ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(pages.size(), 1u);
  BOOST_CHECK(pages[0].size() == QSize(64, 48));
  BOOST_CHECK_EQUAL(pages[0].dpi().horizontal(), 300);
}

// ImageLoader turns JPEGs upright by their EXIF orientation, so the metadata of a photo
// stored landscape with Orientation=6 has to report the upright size and swapped DPI axes.
BOOST_AUTO_TEST_CASE(test_exif_rotated_jpeg_metadata) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("rotated.jpg");
  writeGrayJpeg(path, 64, 48, 300, 200, false, 6);

  std::vector<ImageMetadata> pages;
  const auto status
      = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { pages.push_back(metadata); });
  BOOST_REQUIRE(status == ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(pages.size(), 1u);
  BOOST_CHECK(pages[0].size() == QSize(48, 64));
  BOOST_CHECK_EQUAL(pages[0].dpi().horizontal(), 200);
  BOOST_CHECK_EQUAL(pages[0].dpi().vertical(), 300);
}

// Orientation=3 turns the photo by 180 degrees: the stored size stays.
BOOST_AUTO_TEST_CASE(test_exif_upside_down_jpeg_metadata) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("upside_down.jpg");
  writeGrayJpeg(path, 64, 48, 300, 200, false, 3);

  std::vector<ImageMetadata> pages;
  const auto status
      = ImageMetadataLoader::load(path, [&](const ImageMetadata& metadata) { pages.push_back(metadata); });
  BOOST_REQUIRE(status == ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(pages.size(), 1u);
  BOOST_CHECK(pages[0].size() == QSize(64, 48));
  BOOST_CHECK_EQUAL(pages[0].dpi().horizontal(), 300);
  BOOST_CHECK_EQUAL(pages[0].dpi().vertical(), 200);
}

// The file name says JPEG, the content is a PNG: Qt recognizes it by content, so it has to load.
BOOST_AUTO_TEST_CASE(test_png_named_jpg_loads) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("wrong_name.jpg");
  QImage source(30, 20, QImage::Format_RGB32);
  source.fill(Qt::red);
  BOOST_REQUIRE(source.save(path, "PNG"));

  const QImage image = ImageLoader::load(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK(image.size() == QSize(30, 20));
}

BOOST_AUTO_TEST_CASE(test_damaged_jpeg_metadata_reports_errors) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("damaged.jpg");
  {
    // The start of image, then an APP0 marker with an impossible length.
    const char data[] = {'\xff', '\xd8', '\xff', '\xe0', '\x00', '\x01', '\x00', '\x00'};
    QFile file(path);
    BOOST_REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(data, sizeof(data));
  }

  ImageLoadErrorCapture capture;
  const auto status = ImageMetadataLoader::load(path, [](const ImageMetadata&) {});
  BOOST_CHECK(status != ImageMetadataLoader::LOADED);
  // libjpeg's reason reaches the user, not just "damaged".
  BOOST_CHECK(!capture.messages().isEmpty());
}

BOOST_AUTO_TEST_CASE(test_damaged_tiff_reports_errors) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("damaged.tif");
  TiffSpec spec;
  spec.width = 200;
  spec.height = 200;
  spec.compression = COMPRESSION_LZW;
  spec.rowsPerStrip = 200;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int y, int) { return (x ^ y) & 0xff; }));

  // The only strip is moved behind the end of the file, so reading it fails for sure.
  QByteArray data = Tests::readFile(path);
  BOOST_REQUIRE(setTiffTag(data, ifdOffset(data, 0), TIFFTAG_STRIPOFFSETS, 0x7fffff00));
  BOOST_REQUIRE(Tests::writeFile(path, data));

  QStringList messages;
  const QImage image = readTiff(path, &messages);
  BOOST_CHECK(image.isNull());
  BOOST_CHECK(!messages.isEmpty());
}

// A 16-bit gray scan takes libtiff's RGBA path, but has no alpha channel.
BOOST_AUTO_TEST_CASE(test_16bit_gray_tiff_is_opaque) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("gray16.tif");
  TiffSpec spec;
  spec.width = 20;
  spec.height = 10;
  spec.bitsPerSample = 16;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 40000.0; }));

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK(!image.hasAlphaChannel());
  BOOST_CHECK_LE(std::abs(qGray(image.pixel(7, 3)) - 40000 / 257), 2);
}

// CMYK has four samples, none of them alpha.
BOOST_AUTO_TEST_CASE(test_cmyk_tiff_is_opaque) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("cmyk.tif");
  TiffSpec spec;
  spec.width = 20;
  spec.height = 10;
  spec.samplesPerPixel = 4;
  spec.photometric = PHOTOMETRIC_SEPARATED;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int s) { return s == 0 ? 255.0 : 0.0; }));  // Pure cyan.

  const QImage image = readTiff(path);
  BOOST_REQUIRE(!image.isNull());
  BOOST_CHECK(!image.hasAlphaChannel());
  const QRgb px = image.pixel(7, 3);
  BOOST_CHECK_LE(qRed(px), 2);
  BOOST_CHECK_GE(qGreen(px), 253);
  BOOST_CHECK_GE(qBlue(px), 253);
}

// libtiff allows rationals up to 2^32-1; rounding that to int is undefined behavior.
BOOST_AUTO_TEST_CASE(test_tiff_huge_resolution_is_ignored) {
  for (const uint16_t unit : {uint16_t(RESUNIT_INCH), uint16_t(RESUNIT_CENTIMETER)}) {
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString path = dir.filePath("huge_dpi.tif");
    TiffSpec spec;
    spec.width = 8;
    spec.height = 8;
    spec.resolutionUnit = unit;
    BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
    // libtiff doesn't write such a value itself, so the rationals are set to 4294967295/1.
    QByteArray data = Tests::readFile(path);
    const uint32_t ifd = ifdOffset(data, 0);
    for (uint32_t i = 0; i < readLe(data, ifd, 2); ++i) {
      const uint32_t entry = ifd + 2 + 12 * i;
      const uint32_t tag = readLe(data, entry, 2);
      if ((tag == TIFFTAG_XRESOLUTION) || (tag == TIFFTAG_YRESOLUTION)) {
        const uint32_t rational = readLe(data, entry + 8, 4);
        writeLe(data, rational, 4, 0xffffffffu);
        writeLe(data, rational + 4, 4, 1);
      }
    }
    BOOST_REQUIRE(Tests::writeFile(path, data));

    std::vector<ImageMetadata> pages;
    ImageMetadataLoader::Status status;
    readTiffMetadata(path, pages, status);
    BOOST_REQUIRE(status == ImageMetadataLoader::LOADED);
    BOOST_REQUIRE_EQUAL(pages.size(), 1u);
    BOOST_CHECK_EQUAL(pages[0].dpi().horizontal(), 0);
    BOOST_CHECK_EQUAL(pages[0].dpi().vertical(), 0);
  }
}

// A width beyond INT_MAX would become negative in the page metadata.
BOOST_AUTO_TEST_CASE(test_tiff_width_beyond_int_max_is_rejected) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("huge_width.tif");
  TiffSpec spec;
  spec.width = 8;
  spec.height = 8;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
  QByteArray data = Tests::readFile(path);
  BOOST_REQUIRE(setTiffTag(data, ifdOffset(data, 0), TIFFTAG_IMAGEWIDTH, 0x80000001u));
  BOOST_REQUIRE(Tests::writeFile(path, data));

  std::vector<ImageMetadata> pages;
  ImageMetadataLoader::Status status;
  const QStringList messages = readTiffMetadata(path, pages, status);
  BOOST_CHECK(status != ImageMetadataLoader::LOADED);
  for (const ImageMetadata& page : pages) {
    BOOST_CHECK_GE(page.size().width(), 0);
  }
  BOOST_CHECK(!messages.isEmpty());
}

// Page 2 exists, but its directory lacks the required ImageLength tag.
BOOST_AUTO_TEST_CASE(test_tiff_damaged_page_directory_message) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("damaged_page.tif");
  TiffSpec spec;
  spec.width = 8;
  spec.height = 8;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }, 2));
  QByteArray data = Tests::readFile(path);
  // An unknown private tag takes the place of ImageLength.
  const uint32_t ifd = ifdOffset(data, 1);
  for (uint32_t i = 0; i < readLe(data, ifd, 2); ++i) {
    if (readLe(data, ifd + 2 + 12 * i, 2) == TIFFTAG_IMAGELENGTH) {
      writeLe(data, ifd + 2 + 12 * i, 2, 65000);
    }
  }
  BOOST_REQUIRE(Tests::writeFile(path, data));

  ImageLoadErrorCapture capture;
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  BOOST_CHECK(TiffReader::readImage(file, 1).isNull());
  const QString messages = capture.messages().join(QLatin1Char('\n'));
  BOOST_CHECK_MESSAGE(!messages.contains(QLatin1String("doesn't exist")), messages.toStdString());
  BOOST_CHECK_MESSAGE(messages.contains(QLatin1String("Page 2 of this file is damaged.")), messages.toStdString());
}

// The link from page 2 to page 3 points beyond the end of the file.
BOOST_AUTO_TEST_CASE(test_tiff_damaged_next_directory_is_reported) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  TiffSpec spec;
  spec.width = 8;
  spec.height = 8;

  const QString intactPath = dir.filePath("intact.tif");
  BOOST_REQUIRE(writeTiff(intactPath, spec, [](int, int, int) { return 0.0; }, 3));
  {
    ImageLoadErrorCapture capture;
    std::vector<ImageMetadata> pages;
    BOOST_CHECK_EQUAL(
        ImageMetadataLoader::load(intactPath, [&](const ImageMetadata& metadata) { pages.push_back(metadata); }),
        ImageMetadataLoader::LOADED);
    BOOST_CHECK_EQUAL(pages.size(), 3u);
    BOOST_CHECK(capture.partialReads().isEmpty());
  }

  const QString damagedPath = dir.filePath("damaged_link.tif");
  QByteArray data = Tests::readFile(intactPath);
  const uint32_t ifd = ifdOffset(data, 1);
  writeLe(data, ifd + 2 + 12 * readLe(data, ifd, 2), 4, static_cast<uint32_t>(data.size()) + 4096);
  BOOST_REQUIRE(Tests::writeFile(damagedPath, data));

  ImageLoadErrorCapture capture;
  std::vector<ImageMetadata> pages;
  BOOST_CHECK_EQUAL(
      ImageMetadataLoader::load(damagedPath, [&](const ImageMetadata& metadata) { pages.push_back(metadata); }),
      ImageMetadataLoader::LOADED);
  // The readable pages are taken over, and the loss of the rest is reported.
  BOOST_CHECK_EQUAL(pages.size(), 2u);
  BOOST_CHECK_MESSAGE(capture.partialReads().contains(QLatin1String("Page 3 of this file is damaged.")),
                      capture.partialReads().join(QLatin1Char('\n')).toStdString());
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_round_trip_and_failure) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QImage image(50, 30, QImage::Format_Indexed8);
  image.setColorCount(256);
  for (int i = 0; i < 256; ++i) {
    image.setColor(i, qRgb(i, i, i));
  }
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.scanLine(y)[x] = static_cast<uint8_t>(x * 5 + y);
    }
  }

  const QString goodPath = dir.filePath("written.tif");
  BOOST_REQUIRE(TiffWriter::writeImage(goodPath, image));
  const QImage readBack = readTiff(goodPath);
  BOOST_REQUIRE(!readBack.isNull());
  BOOST_CHECK_EQUAL(grayAt(readBack, 7, 11), 7 * 5 + 11);

  // Writing into a directory that doesn't exist has to fail, be reported,
  // and must not leave a file behind.
  QStringList reported;
  const auto connection
      = QObject::connect(&ImageLoadErrorReporter::instance(), &ImageLoadErrorReporter::imageWriteFailed,
                         [&](const QString&, const QStringList& messages) { reported = messages; });
  const QString badPath = dir.filePath("missing/written.tif");
  BOOST_CHECK(!TiffWriter::writeImage(badPath, image));
  QObject::disconnect(connection);
  BOOST_CHECK(!reported.isEmpty());
  BOOST_CHECK(!QFile::exists(badPath));
}

bool writeTiffWith(const QString& path, const QImage& image, const TiffWriter::Compression& compression) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  return TiffWriter::writeImage(file, image, compression);
}

struct WrittenTags {
  uint16_t compression = 0;
  uint16_t photometric = 0;
  uint16_t bitsPerSample = 0;
  // Without compression, libtiff has no predictor tag at all and reports 0.
  uint16_t predictor = PREDICTOR_NONE;
};

WrittenTags readWrittenTags(const QString& path) {
  WrittenTags tags;
  TIFF* tif = Tests::openTiffFile(path, "r");
  if (tif) {
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &tags.compression);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &tags.photometric);
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &tags.bitsPerSample);
    TIFFGetFieldDefaulted(tif, TIFFTAG_PREDICTOR, &tags.predictor);
    if (tags.predictor == 0) {
      tags.predictor = PREDICTOR_NONE;
    }
    TIFFClose(tif);
  }
  return tags;
}

bool isNear(const QRgb actual, const QRgb expected, const int tolerance) {
  return (std::abs(qRed(actual) - qRed(expected)) <= tolerance)
         && (std::abs(qGreen(actual) - qGreen(expected)) <= tolerance)
         && (std::abs(qBlue(actual) - qBlue(expected)) <= tolerance);
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_marks_alpha) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("alpha.tif");
  QImage image(20, 10, QImage::Format_ARGB32);
  image.fill(qRgba(200, 100, 50, 128));
  BOOST_REQUIRE(TiffWriter::writeImage(path, image));

  uint16_t count = 0;
  const uint16_t* kinds = nullptr;
  TIFF* tif = Tests::openTiffFile(path, "r");
  BOOST_REQUIRE(tif);
  const bool hasTag = TIFFGetField(tif, TIFFTAG_EXTRASAMPLES, &count, &kinds) != 0;
  const uint16_t kind = (hasTag && (count == 1)) ? kinds[0] : uint16_t(EXTRASAMPLE_UNSPECIFIED);
  TIFFClose(tif);
  BOOST_CHECK_EQUAL(kind, EXTRASAMPLE_UNASSALPHA);

  const QImage readBack = readTiff(path);
  BOOST_REQUIRE(!readBack.isNull());
  const QRgb px = readBack.convertToFormat(QImage::Format_ARGB32).pixel(7, 3);
  BOOST_CHECK_EQUAL(qAlpha(px), 128);
  BOOST_CHECK(isNear(px, qRgb(200, 100, 50), 2));
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_jpeg_color_is_ycbcr) {
  if (!TIFFIsCODECConfigured(COMPRESSION_JPEG)) {
    BOOST_TEST_MESSAGE("libtiff has no JPEG support, skipped");
    return;
  }
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QRgb red = qRgb(200, 30, 30);
  const QRgb blue = qRgb(30, 60, 200);
  QImage image(64, 48, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixel(x, y, x < 32 ? red : blue);
    }
  }

  const QString path = dir.filePath("jpeg_color.tif");
  BOOST_REQUIRE(writeTiffWith(path, image, {COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 85}));
  const WrittenTags tags = readWrittenTags(path);
  BOOST_CHECK_EQUAL(tags.compression, COMPRESSION_JPEG);
  BOOST_CHECK_EQUAL(tags.photometric, PHOTOMETRIC_YCBCR);

  const QImage readBack = readTiff(path);
  BOOST_REQUIRE(!readBack.isNull());
  BOOST_CHECK(readBack.size() == image.size());
  // Lossy, so only roughly.
  BOOST_CHECK(isNear(readBack.pixel(10, 24), red, 16));
  BOOST_CHECK(isNear(readBack.pixel(54, 24), blue, 16));
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_jpeg_quality) {
  if (!TIFFIsCODECConfigured(COMPRESSION_JPEG)) {
    BOOST_TEST_MESSAGE("libtiff has no JPEG support, skipped");
    return;
  }
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // Fine detail, so the quality makes a difference.
  QImage image(128, 128, QImage::Format_RGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixel(x, y, qRgb((x * 37 + y * 11) & 255, (x * 13 ^ y * 29) & 255, (x * y) & 255));
    }
  }

  const QString lowPath = dir.filePath("low.tif");
  const QString highPath = dir.filePath("high.tif");
  BOOST_REQUIRE(writeTiffWith(lowPath, image, {COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 20}));
  BOOST_REQUIRE(writeTiffWith(highPath, image, {COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 95}));
  BOOST_CHECK(QFileInfo(lowPath).size() < QFileInfo(highPath).size());
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_jpeg_gray) {
  if (!TIFFIsCODECConfigured(COMPRESSION_JPEG)) {
    BOOST_TEST_MESSAGE("libtiff has no JPEG support, skipped");
    return;
  }
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QImage image(64, 48, QImage::Format_Indexed8);
  image.setColorCount(256);
  for (int i = 0; i < 256; ++i) {
    image.setColor(i, qRgb(i, i, i));
  }
  for (int y = 0; y < image.height(); ++y) {
    std::memset(image.scanLine(y), 0, 32);
    std::memset(image.scanLine(y) + 32, 220, 32);
  }

  const QString path = dir.filePath("jpeg_gray.tif");
  BOOST_REQUIRE(writeTiffWith(path, image, {COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 85}));
  const WrittenTags tags = readWrittenTags(path);
  BOOST_CHECK_EQUAL(tags.compression, COMPRESSION_JPEG);
  BOOST_CHECK_EQUAL(tags.photometric, PHOTOMETRIC_MINISBLACK);

  const QImage readBack = readTiff(path);
  BOOST_REQUIRE(!readBack.isNull());
  BOOST_CHECK(std::abs(grayAt(readBack, 10, 24) - 0) < 12);
  BOOST_CHECK(std::abs(grayAt(readBack, 54, 24) - 220) < 12);
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_jpeg_falls_back_to_lzw) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const TiffWriter::Compression jpeg{COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 85, COMPRESSION_JPEG};

  // A palette image, as posterizing produces. JPEG can't store it.
  QImage palette(40, 30, QImage::Format_Indexed8);
  palette.setColorCount(3);
  palette.setColor(0, qRgb(255, 255, 255));
  palette.setColor(1, qRgb(200, 0, 0));
  palette.setColor(2, qRgb(0, 0, 160));
  for (int y = 0; y < palette.height(); ++y) {
    for (int x = 0; x < palette.width(); ++x) {
      palette.scanLine(y)[x] = static_cast<uint8_t>((x / 10) % 3);
    }
  }
  const QString palettePath = dir.filePath("palette.tif");
  BOOST_REQUIRE(writeTiffWith(palettePath, palette, jpeg));
  const WrittenTags paletteTags = readWrittenTags(palettePath);
  BOOST_CHECK_EQUAL(paletteTags.compression, COMPRESSION_LZW);
  BOOST_CHECK_EQUAL(paletteTags.photometric, PHOTOMETRIC_PALETTE);
  const QImage paletteBack = readTiff(palettePath);
  BOOST_REQUIRE(!paletteBack.isNull());
  BOOST_CHECK(paletteBack.pixel(15, 5) == qRgb(200, 0, 0));
  BOOST_CHECK(paletteBack.pixel(25, 5) == qRgb(0, 0, 160));

  // JPEG can't store an alpha channel either.
  QImage alpha(20, 10, QImage::Format_ARGB32);
  alpha.fill(qRgba(10, 20, 30, 128));
  const QString alphaPath = dir.filePath("alpha.tif");
  BOOST_REQUIRE(writeTiffWith(alphaPath, alpha, jpeg));
  BOOST_CHECK_EQUAL(readWrittenTags(alphaPath).compression, COMPRESSION_LZW);
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_bitonal_compressions) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QImage image = Tests::whiteMonoImage(40, 30);
  for (int y = 5; y < 25; ++y) {
    for (int x = 8; x < 20; ++x) {
      image.setPixel(x, y, 1);
    }
  }

  for (const int method :
       {COMPRESSION_CCITTFAX4, COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA, COMPRESSION_NONE}) {
    if (!TIFFIsCODECConfigured(uint16_t(method))) {
      continue;
    }
    const QString path = dir.filePath(QStringLiteral("bw_%1.tif").arg(method));
    BOOST_REQUIRE(writeTiffWith(path, image, {method, COMPRESSION_JPEG, 85}));
    BOOST_CHECK_EQUAL(readWrittenTags(path).compression, method);
    const QImage readBack = readTiff(path);
    BOOST_REQUIRE(!readBack.isNull());
    BOOST_CHECK_EQUAL(grayAt(readBack, 10, 10), 0);
    BOOST_CHECK_EQUAL(grayAt(readBack, 30, 10), 255);
  }
}

QImage makePaletteImage(const int numColors) {
  QImage image(41, 30, QImage::Format_Indexed8);
  image.setColorCount(numColors);
  for (int i = 0; i < numColors; ++i) {
    image.setColor(i, qRgb((i * 53) & 255, (i * 97) & 255, 255 - i * 7));
  }
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.scanLine(y)[x] = static_cast<uint8_t>((x + y) % numColors);
    }
  }
  return image;
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_palette_compression_and_depth) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  // The palette setting applies, not the one for color. An odd width tests the last half byte.
  for (const int method : {COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA, COMPRESSION_NONE}) {
    if (!TIFFIsCODECConfigured(uint16_t(method))) {
      continue;
    }
    for (const int numColors : {3, 16, 17}) {
      const QImage image = makePaletteImage(numColors);
      const QString path = dir.filePath(QStringLiteral("palette_%1_%2.tif").arg(method).arg(numColors));
      BOOST_REQUIRE(writeTiffWith(path, image, {COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 85, method}));
      const WrittenTags tags = readWrittenTags(path);
      BOOST_CHECK_EQUAL(tags.compression, method);
      BOOST_CHECK_EQUAL(tags.photometric, PHOTOMETRIC_PALETTE);
      BOOST_CHECK_EQUAL(tags.bitsPerSample, (numColors <= 16) ? 4 : 8);
      BOOST_CHECK_EQUAL(tags.predictor, PREDICTOR_NONE);

      const QImage readBack = readTiff(path);
      BOOST_REQUIRE(!readBack.isNull());
      for (const QPoint& p : {QPoint(0, 0), QPoint(1, 0), QPoint(40, 0), QPoint(40, 29), QPoint(17, 11)}) {
        BOOST_CHECK(readBack.pixel(p) == image.pixel(p));
      }
    }
  }
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_predictor) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  QImage color(64, 40, QImage::Format_RGB32);
  for (int y = 0; y < color.height(); ++y) {
    for (int x = 0; x < color.width(); ++x) {
      color.setPixel(x, y, qRgb(x * 4, y * 6, (x * y) & 255));
    }
  }
  QImage gray(64, 40, QImage::Format_Indexed8);
  gray.setColorCount(256);
  for (int i = 0; i < 256; ++i) {
    gray.setColor(i, qRgb(i, i, i));
  }
  for (int y = 0; y < gray.height(); ++y) {
    for (int x = 0; x < gray.width(); ++x) {
      gray.scanLine(y)[x] = static_cast<uint8_t>(x * 3 + y);
    }
  }

  for (const int method : {COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_LZMA, COMPRESSION_NONE}) {
    if (!TIFFIsCODECConfigured(uint16_t(method))) {
      continue;
    }
    const uint16_t expectedPredictor = (method == COMPRESSION_NONE) ? PREDICTOR_NONE : PREDICTOR_HORIZONTAL;

    const QString colorPath = dir.filePath(QStringLiteral("color_%1.tif").arg(method));
    BOOST_REQUIRE(writeTiffWith(colorPath, color, {COMPRESSION_CCITTFAX4, method, 85}));
    BOOST_CHECK_EQUAL(readWrittenTags(colorPath).predictor, expectedPredictor);
    const QImage colorBack = readTiff(colorPath);
    BOOST_REQUIRE(!colorBack.isNull());
    BOOST_CHECK(colorBack.pixel(37, 21) == color.pixel(37, 21));
    BOOST_CHECK(colorBack.pixel(63, 39) == color.pixel(63, 39));

    const QString grayPath = dir.filePath(QStringLiteral("gray_%1.tif").arg(method));
    BOOST_REQUIRE(writeTiffWith(grayPath, gray, {COMPRESSION_CCITTFAX4, method, 85}));
    const WrittenTags grayTags = readWrittenTags(grayPath);
    BOOST_CHECK_EQUAL(grayTags.photometric, PHOTOMETRIC_MINISBLACK);
    BOOST_CHECK_EQUAL(grayTags.predictor, expectedPredictor);
    const QImage grayBack = readTiff(grayPath);
    BOOST_REQUIRE(!grayBack.isNull());
    BOOST_CHECK_EQUAL(grayAt(grayBack, 37, 21), 37 * 3 + 21);
  }

  // Not for black and white images.
  QImage bw = Tests::whiteMonoImage(40, 30);
  const QString bwPath = dir.filePath("bw_lzw.tif");
  BOOST_REQUIRE(writeTiffWith(bwPath, bw, {COMPRESSION_LZW, COMPRESSION_LZW, 85}));
  BOOST_CHECK_EQUAL(readWrittenTags(bwPath).predictor, PREDICTOR_NONE);
}

BOOST_AUTO_TEST_CASE(test_compression_names) {
  BOOST_CHECK(TiffReader::compressionName(COMPRESSION_LZW) == "LZW");
  BOOST_CHECK(TiffReader::compressionName(50002) == "JPEG XL");
  BOOST_CHECK(TiffReader::compressionName(65000) == "#65000");
}

BOOST_AUTO_TEST_CASE(test_error_capture) {
  ImageLoadErrorCapture outer;
  {
    ImageLoadErrorCapture inner;
    ImageLoadErrorCapture::addWarning("warning");
    BOOST_CHECK(inner.messages() == QStringList("warning"));
    ImageLoadErrorCapture::addError("error");
    ImageLoadErrorCapture::addError("error");
    BOOST_CHECK(inner.messages() == QStringList("error"));
  }
  BOOST_CHECK(outer.messages().isEmpty());
}

BOOST_AUTO_TEST_CASE(test_jp2_gray_and_rgb) {
  for (const int numComps : {1, 3}) {
    for (const bool codestream : {false, true}) {
      QTemporaryDir dir;
      BOOST_REQUIRE(dir.isValid());
      const QString path = dir.filePath(codestream ? "image.j2k" : "image.jp2");
      BOOST_REQUIRE(writeJp2(path, 64, 48, numComps, codestream));

      QFile file(path);
      BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
      BOOST_REQUIRE(Jp2Reader::canRead(file));

      std::vector<ImageMetadata> pages;
      BOOST_REQUIRE(Jp2Reader::readMetadata(
                        file, ProxyFunction<std::function<void(const ImageMetadata&)>, void, const ImageMetadata&>(
                                  [&](const ImageMetadata& m) { pages.push_back(m); }))
                    == ImageMetadataLoader::LOADED);
      BOOST_REQUIRE_EQUAL(pages.size(), 1u);
      BOOST_CHECK(pages[0].size() == QSize(64, 48));

      const QImage image = Jp2Reader::readImage(file);
      BOOST_REQUIRE(!image.isNull());
      BOOST_CHECK(image.size() == QSize(64, 48));
      const QRgb px = image.pixel(10, 7);
      if (numComps == 1) {
        BOOST_CHECK(image.format() == QImage::Format_Indexed8);
        BOOST_CHECK_EQUAL(qGray(px), (10 * 4 + 7 * 2) % 256);
      } else {
        BOOST_CHECK_EQUAL(qRed(px), (10 * 4 + 7 * 2) % 256);
        BOOST_CHECK_EQUAL(qGreen(px), (10 * 4 + 7 * 2 + 60) % 256);
        BOOST_CHECK_EQUAL(qBlue(px), (10 * 4 + 7 * 2 + 120) % 256);
      }

      // Reduced resolution for thumbnails.
      const QImage reduced = Jp2Reader::readImage(file, QSize(10, 10));
      BOOST_REQUIRE(!reduced.isNull());
      BOOST_CHECK(reduced.size() == QSize(16, 12));
    }
  }
}

BOOST_AUTO_TEST_CASE(test_jp2_strip_decoding) {
  for (const int numComps : {1, 3}) {
    QTemporaryDir dir;
    BOOST_REQUIRE(dir.isValid());
    const QString path = dir.filePath("strips.jp2");
    BOOST_REQUIRE(writeJp2(path, 120, 100, numComps, false));

    QFile file(path);
    BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
    const QImage whole = Jp2Reader::readImage(file);
    BOOST_REQUIRE(!whole.isNull());

    // Force strip decoding with strips of 16 rows, the last one being shorter.
    QImage stripped;
    uint64_t decodedStrips = 0;
    {
      const Jp2Reader::StripDecodingOverride stripOverride(0, 1, 16);
      stripped = Jp2Reader::readImage(file);
      decodedStrips = stripOverride.decodedStrips();
    }

    BOOST_REQUIRE(!stripped.isNull());
    BOOST_CHECK_EQUAL(decodedStrips, 7u);  // 100 rows: 6 strips of 16 rows, the last one of 4
    BOOST_CHECK(stripped.format() == whole.format());
    BOOST_CHECK(stripped == whole);
  }
}

BOOST_AUTO_TEST_CASE(test_jp2_not_recognized) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("not_jp2.tif");
  TiffSpec spec;
  spec.width = 4;
  spec.height = 4;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  BOOST_CHECK(!Jp2Reader::canRead(file));
}

namespace {
// Loads the image the way pages and thumbnails are loaded and returns the messages.
QStringList loadMessages(const QString& path, QImage& image, const QSize& minSize = QSize()) {
  ImageLoadErrorCapture capture;
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  image = ImageLoader::load(file, 0, minSize);
  return capture.messages();
}

QStringList metadataMessages(const QString& path, ImageMetadataLoader::Status& status) {
  std::vector<ImageMetadata> pages;
  return readTiffMetadata(path, pages, status);  // Works for all formats.
}

// Neither decoding nor reading the metadata accepts the image, and both say why.
void checkRefusedAsTooLarge(const QString& path, const QSize& size) {
  const QString expected = ImageSizeLimit::errorMessage(size);
  QImage image;
  BOOST_CHECK(loadMessages(path, image).contains(expected));
  BOOST_CHECK(image.isNull());
  BOOST_CHECK(loadMessages(path, image, QSize(100, 100)).contains(expected));
  BOOST_CHECK(image.isNull());

  ImageMetadataLoader::Status status;
  BOOST_CHECK(metadataMessages(path, status).contains(expected));
  BOOST_CHECK(status != ImageMetadataLoader::LOADED);
}

// Sizes over the limit whose pixels are still cheap to hold, should a check be missing.
const QSize TOO_WIDE(ImageSizeLimit::MAX_SIDE + 1, 64);
}  // namespace

// The header claims a size over the limit; the file has far fewer pixels.
BOOST_AUTO_TEST_CASE(test_tiff_over_size_limit_is_refused) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  TiffSpec spec;
  spec.width = 8;
  spec.height = 8;
  spec.bitsPerSample = 1;
  // 25000 x 25000 has more pixels than allowed, but both sides are allowed. As 1 bit image,
  // its pixels need 78 MB.
  for (const QSize& size : {TOO_WIDE, QSize(25000, 25000)}) {
    BOOST_REQUIRE(ImageSizeLimit::exceeds(size));
    const QString path = dir.filePath(QStringLiteral("too_large_%1.tif").arg(size.width()));
    BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
    QByteArray data = Tests::readFile(path);
    BOOST_REQUIRE(claimTiffSize(data, size));
    BOOST_REQUIRE(Tests::writeFile(path, data));
    checkRefusedAsTooLarge(path, size);
  }
}

BOOST_AUTO_TEST_CASE(test_png_over_size_limit_is_refused) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("too_wide.png");
  QImage image(TOO_WIDE, QImage::Format_Mono);
  image.fill(0);
  BOOST_REQUIRE(image.save(path, "PNG"));
  checkRefusedAsTooLarge(path, TOO_WIDE);
}

BOOST_AUTO_TEST_CASE(test_jpeg_over_size_limit_is_refused) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("too_wide.jpg");
  writeGrayJpeg(path, TOO_WIDE.width(), TOO_WIDE.height(), 300, 300, false);
  checkRefusedAsTooLarge(path, TOO_WIDE);
}

BOOST_AUTO_TEST_CASE(test_jp2_over_size_limit_is_refused) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  for (const bool codestream : {false, true}) {
    const QString path = dir.filePath(codestream ? "too_wide.j2k" : "too_wide.jp2");
    BOOST_REQUIRE(writeJp2(path, TOO_WIDE.width(), TOO_WIDE.height(), 1, codestream));
    checkRefusedAsTooLarge(path, TOO_WIDE);
  }
}

// OpenJPEG allocates its parameters for every tile while reading the header, before Jp2Reader
// knows the size: a 262-byte codestream from fuzzing claiming 16 x 393252 pixels in 32771 tiles
// cost 186 MB. So the size has to be checked before OpenJPEG sees the header. These headers have
// tiles of one pixel, more than OpenJPEG allows; its error shows that it was asked.
BOOST_AUTO_TEST_CASE(test_jp2_over_size_limit_is_refused_before_openjpeg) {
  const QSize size(16, 40000);
  BOOST_REQUIRE(ImageSizeLimit::exceeds(size));
  const unsigned char codestream[] = {
      0xFF, 0x4F,              // SOC
      0xFF, 0x51, 0x00, 0x29,  // SIZ, length 41
      0x00, 0x00,              // Rsiz
      0x00, 0x00, 0x00, 0x10,  // Xsiz: 16
      0x00, 0x00, 0x9C, 0x40,  // Ysiz: 40000
      0x00, 0x00, 0x00, 0x00,  // XOsiz
      0x00, 0x00, 0x00, 0x00,  // YOsiz
      0x00, 0x00, 0x00, 0x01,  // XTsiz: 1
      0x00, 0x00, 0x00, 0x01,  // YTsiz: 1
      0x00, 0x00, 0x00, 0x00,  // XTOsiz
      0x00, 0x00, 0x00, 0x00,  // YTOsiz
      0x00, 0x01,              // Csiz: 1 component
      0x07, 0x01, 0x01,        // Ssiz: 8 bits, XRsiz, YRsiz
  };

  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString j2kPath = dir.filePath("forged.j2k");
  BOOST_REQUIRE(Tests::writeFile(
      j2kPath, QByteArray(reinterpret_cast<const char*>(codestream), static_cast<int>(sizeof(codestream)))));

  // In a JP2 file, the codestream's size counts, not the one of the header box (16 x 16 here).
  const QString jp2Path = dir.filePath("forged.jp2");
  BOOST_REQUIRE(writeJp2(jp2Path, 16, 16, 1, false));
  QByteArray jp2 = Tests::readFile(jp2Path);
  const auto sizPos = jp2.indexOf(QByteArray(reinterpret_cast<const char*>(codestream), 4));
  BOOST_REQUIRE(sizPos > 0);
  jp2.replace(sizPos, 36, reinterpret_cast<const char*>(codestream), 36);  // Up to YTOsiz.
  BOOST_REQUIRE(Tests::writeFile(jp2Path, jp2));

  for (const QString& path : {j2kPath, jp2Path}) {
    BOOST_TEST_CONTEXT(path.toStdString()) {
      QImage image;
      const QStringList messages = loadMessages(path, image);
      BOOST_CHECK(image.isNull());
      BOOST_CHECK(messages.contains(ImageSizeLimit::errorMessage(size)));
      BOOST_CHECK(messages.filter(QStringLiteral("OpenJPEG")).isEmpty());

      // Adding the file refuses it as well, by the size of the codestream.
      ImageMetadataLoader::Status status;
      BOOST_CHECK(metadataMessages(path, status).contains(ImageSizeLimit::errorMessage(size)));
      BOOST_CHECK(status != ImageMetadataLoader::LOADED);
    }
  }
}

namespace {
// A codestream of just SOC and SIZ with one 8-bit component. Its fields are set by the tests.
struct SizSpec {
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t tileWidth = 0;
  uint32_t tileHeight = 0;
  uint16_t numComps = 1;  // Csiz; the length of the marker always fits one component.
};

QByteArray sizOnlyCodestream(const SizSpec& spec) {
  QByteArray data;
  auto put16 = [&](const uint16_t v) {
    data.append(static_cast<char>(v >> 8));
    data.append(static_cast<char>(v & 0xFF));
  };
  auto put32 = [&](const uint32_t v) {
    put16(static_cast<uint16_t>(v >> 16));
    put16(static_cast<uint16_t>(v & 0xFFFF));
  };
  put16(0xFF4F);  // SOC
  put16(0xFF51);  // SIZ
  put16(41);      // Lsiz: 38 + 3 bytes for one component
  put16(0);       // Rsiz
  put32(spec.width);
  put32(spec.height);
  put32(0);  // XOsiz
  put32(0);  // YOsiz
  put32(spec.tileWidth);
  put32(spec.tileHeight);
  put32(0);  // XTOsiz
  put32(0);  // YTOsiz
  put16(spec.numComps);
  data.append("\x07\x01\x01", 3);  // Ssiz: 8 bits, XRsiz, YRsiz
  return data;
}

// Loads the codestream and checks that it was refused with \p expected before OpenJPEG saw it,
// and that reading its metadata refuses it as well.
void checkRefusedBeforeOpenJpeg(const QByteArray& codestream, const QString& expected) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("forged.j2k");
  BOOST_REQUIRE(Tests::writeFile(path, codestream));
  QImage image;
  const QStringList messages = loadMessages(path, image);
  BOOST_CHECK(image.isNull());
  BOOST_CHECK(messages.contains(expected));
  BOOST_CHECK(messages.filter(QStringLiteral("OpenJPEG")).isEmpty());

  // Adding the file refuses it for the same reason.
  ImageMetadataLoader::Status status;
  BOOST_CHECK(metadataMessages(path, status).contains(expected));
  BOOST_CHECK(status != ImageMetadataLoader::LOADED);
}
}  // namespace

// Within the size limit, OpenJPEG still allocates about 5.7 KB per tile and 1.1 KB per tile and
// component while reading the header, and decoding keeps 4 bytes per pixel and component; a forged
// header could ask for terabytes. These headers contain an error OpenJPEG stops at before
// allocating, so its error shows that it was asked.
BOOST_AUTO_TEST_CASE(test_jp2_too_many_components_are_refused_before_openjpeg) {
  SizSpec spec;
  spec.width = 20000;
  spec.height = 20000;
  spec.tileWidth = 20000;
  spec.tileHeight = 20000;
  spec.numComps = 7;  // More than the length of the marker holds.
  BOOST_REQUIRE(!ImageSizeLimit::exceeds(QSize(20000, 20000)));
  checkRefusedBeforeOpenJpeg(sizOnlyCodestream(spec),
                             QStringLiteral("The image has too many components (7). At its size of 20000 × 20000 "
                                            "pixels, at most 6 components are possible."));
}

BOOST_AUTO_TEST_CASE(test_jp2_too_many_tiles_are_refused_before_openjpeg) {
  SizSpec spec;
  spec.width = 300;
  spec.height = 300;
  spec.tileWidth = 1;  // 90000 tiles, more than OpenJPEG allows.
  spec.tileHeight = 1;
  checkRefusedBeforeOpenJpeg(sizOnlyCodestream(spec),
                             QStringLiteral("The image is divided into too many tiles (90000). At its size of "
                                            "300 × 300 pixels, at most 121 tiles are possible."));
}

// The tile grid may start before the image, and small images may have tiles smaller than 32 px:
// both give one partial tile more per direction than the image size divided by 32.
BOOST_AUTO_TEST_CASE(test_jp2_valid_tile_grids_beyond_the_average_are_accepted) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  struct Case {
    const char* name;
    int size;
    int tileSize;
    int imageOffset;
  };
  // 64 x 64 at offset 16 with tiles of 32: 3 x 3 tiles. 20 x 20 with tiles of 16: 2 x 2 tiles.
  for (const Case& c : {Case{"offset.j2k", 64, 32, 16}, Case{"small.j2k", 20, 16, 0}}) {
    const QString path = dir.filePath(QString::fromLatin1(c.name));
    BOOST_REQUIRE(writeJp2(path, c.size, c.size, 1, true, c.tileSize, c.imageOffset));
    QImage image;
    const QStringList messages = loadMessages(path, image);
    BOOST_CHECK_MESSAGE(!image.isNull(), c.name << ": " << messages.join('\n').toStdString());
    BOOST_CHECK(image.size() == QSize(c.size, c.size));
  }
}

// Tiles of 32 x 32 pixels, the smallest allowed on average, with partial tiles at the edges.
BOOST_AUTO_TEST_CASE(test_jp2_small_tiles_are_accepted) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("small_tiles.j2k");
  BOOST_REQUIRE(writeJp2(path, 70, 40, 3, true, 32));
  QImage image;
  const QStringList messages = loadMessages(path, image);
  BOOST_CHECK_MESSAGE(!image.isNull(), messages.join('\n').toStdString());
  BOOST_CHECK(image.size() == QSize(70, 40));
}

BOOST_AUTO_TEST_CASE(test_image_at_size_limit_is_accepted) {
  // Only the metadata: decoding an image of this size would take too long and too much memory.
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());
  const QString path = dir.filePath("at_limit.tif");
  TiffSpec spec;
  spec.width = 8;
  spec.height = 8;
  spec.bitsPerSample = 1;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
  QByteArray data = Tests::readFile(path);
  BOOST_REQUIRE(claimTiffSize(data, QSize(20000, 30000)));
  BOOST_REQUIRE(Tests::writeFile(path, data));

  std::vector<ImageMetadata> pages;
  ImageMetadataLoader::Status status;
  readTiffMetadata(path, pages, status);
  BOOST_CHECK(status == ImageMetadataLoader::LOADED);
  BOOST_REQUIRE_EQUAL(pages.size(), 1u);
  BOOST_CHECK(pages.front().size() == QSize(20000, 30000));
}

BOOST_AUTO_TEST_SUITE_END()
