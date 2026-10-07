// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageLoadErrors.h>
#include <ImageMetadata.h>
#include <ImageMetadataLoader.h>
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
#include <cstring>
#include <functional>
#include <vector>

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

bool writeTiff(const QString& path, const TiffSpec& spec, const SampleFunc& sample) {
  TIFF* tif = TIFFOpen(QFile::encodeName(path).constData(), spec.bigEndian ? "wb" : "wl");
  if (!tif) {
    return false;
  }
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
  TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
  if (!spec.colormap.empty()) {
    const size_t n = spec.colormap.size() / 3;
    TIFFSetField(tif, TIFFTAG_COLORMAP, spec.colormap.data(), spec.colormap.data() + n, spec.colormap.data() + 2 * n);
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
  TIFFClose(tif);
  return ok;
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

bool writeJp2(const QString& path, const int width, const int height, const int numComps, const bool codestream) {
  std::vector<opj_image_cmptparm_t> params(numComps);
  for (auto& p : params) {
    std::memset(&p, 0, sizeof(p));
    p.dx = 1;
    p.dy = 1;
    p.w = width;
    p.h = height;
    p.prec = 8;
    p.sgnd = 0;
  }
  opj_image_t* image = opj_image_create(numComps, params.data(), numComps == 1 ? OPJ_CLRSPC_GRAY : OPJ_CLRSPC_SRGB);
  if (!image) {
    return false;
  }
  image->x0 = 0;
  image->y0 = 0;
  image->x1 = width;
  image->y1 = height;
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

  opj_codec_t* codec = opj_create_compress(codestream ? OPJ_CODEC_J2K : OPJ_CODEC_JP2);
  bool ok = codec && opj_setup_encoder(codec, &cparams, image);
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
}  // namespace

BOOST_AUTO_TEST_SUITE(CoreImageReadersTestSuite)

BOOST_AUTO_TEST_CASE(test_tiled_bilevel_tiff) {
  QTemporaryDir dir;
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

BOOST_AUTO_TEST_CASE(test_lzma_and_zstd_tiff) {
  for (const uint16_t compression : {uint16_t(COMPRESSION_LZMA), uint16_t(COMPRESSION_ZSTD)}) {
    if (!TIFFIsCODECConfigured(compression)) {
      BOOST_TEST_MESSAGE("libtiff lacks codec " << compression << ", skipped");
      continue;
    }
    QTemporaryDir dir;
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

BOOST_AUTO_TEST_CASE(test_damaged_tiff_reports_errors) {
  QTemporaryDir dir;
  const QString path = dir.filePath("damaged.tif");
  TiffSpec spec;
  spec.width = 200;
  spec.height = 200;
  spec.compression = COMPRESSION_LZW;
  spec.rowsPerStrip = 200;
  BOOST_REQUIRE(writeTiff(path, spec, [](int x, int y, int) { return (x ^ y) & 0xff; }));

  // Scramble the compressed data, which starts right after the 8-byte header.
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadWrite));
  QByteArray data = file.readAll();
  for (int i = 8; i < 400 && i < data.size(); ++i) {
    data[i] = static_cast<char>(0xff);
  }
  file.seek(0);
  file.write(data);
  file.close();

  QStringList messages;
  const QImage image = readTiff(path, &messages);
  if (image.isNull()) {
    BOOST_CHECK(!messages.isEmpty());
  }
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_round_trip_and_failure) {
  QTemporaryDir dir;
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
};

WrittenTags readWrittenTags(const QString& path) {
  WrittenTags tags;
  TIFF* tif = TIFFOpen(QFile::encodeName(path).constData(), "r");
  if (tif) {
    TIFFGetField(tif, TIFFTAG_COMPRESSION, &tags.compression);
    TIFFGetField(tif, TIFFTAG_PHOTOMETRIC, &tags.photometric);
    TIFFClose(tif);
  }
  return tags;
}

bool isNear(const QRgb actual, const QRgb expected, const int tolerance) {
  return (std::abs(qRed(actual) - qRed(expected)) <= tolerance)
         && (std::abs(qGreen(actual) - qGreen(expected)) <= tolerance)
         && (std::abs(qBlue(actual) - qBlue(expected)) <= tolerance);
}

BOOST_AUTO_TEST_CASE(test_tiff_writer_jpeg_color_is_ycbcr) {
  if (!TIFFIsCODECConfigured(COMPRESSION_JPEG)) {
    BOOST_TEST_MESSAGE("libtiff has no JPEG support, skipped");
    return;
  }
  QTemporaryDir dir;
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
  const TiffWriter::Compression jpeg{COMPRESSION_CCITTFAX4, COMPRESSION_JPEG, 85};

  // A palette image, as posterizing produces.  JPEG can't store it.
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
  QImage image(40, 30, QImage::Format_Mono);
  image.setColorCount(2);
  image.setColor(0, qRgb(255, 255, 255));
  image.setColor(1, qRgb(0, 0, 0));
  image.fill(0);
  for (int y = 5; y < 25; ++y) {
    for (int x = 8; x < 20; ++x) {
      image.setPixel(x, y, 1);
    }
  }

  for (const int method : {COMPRESSION_CCITTFAX4, COMPRESSION_LZW, COMPRESSION_DEFLATE, COMPRESSION_NONE}) {
    const QString path = dir.filePath(QStringLiteral("bw_%1.tif").arg(method));
    BOOST_REQUIRE(writeTiffWith(path, image, {method, COMPRESSION_JPEG, 85}));
    BOOST_CHECK_EQUAL(readWrittenTags(path).compression, method);
    const QImage readBack = readTiff(path);
    BOOST_REQUIRE(!readBack.isNull());
    BOOST_CHECK_EQUAL(grayAt(readBack, 10, 10), 0);
    BOOST_CHECK_EQUAL(grayAt(readBack, 30, 10), 255);
  }
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
    const QString path = dir.filePath("strips.jp2");
    BOOST_REQUIRE(writeJp2(path, 120, 100, numComps, false));

    QFile file(path);
    BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
    const QImage whole = Jp2Reader::readImage(file);
    BOOST_REQUIRE(!whole.isNull());

    // Force strip decoding with strips of 16 rows, the last one being shorter.
    Jp2Reader::setStripDecodingParameters(0, 1, 16);
    const QImage stripped = Jp2Reader::readImage(file);
    Jp2Reader::setStripDecodingParameters(uint64_t(1) << 30, uint64_t(128) << 20, 256);

    BOOST_REQUIRE(!stripped.isNull());
    BOOST_CHECK(stripped.format() == whole.format());
    BOOST_CHECK(stripped == whole);
  }
}

BOOST_AUTO_TEST_CASE(test_jp2_not_recognized) {
  QTemporaryDir dir;
  const QString path = dir.filePath("not_jp2.tif");
  TiffSpec spec;
  spec.width = 4;
  spec.height = 4;
  BOOST_REQUIRE(writeTiff(path, spec, [](int, int, int) { return 0.0; }));
  QFile file(path);
  BOOST_REQUIRE(file.open(QIODevice::ReadOnly));
  BOOST_CHECK(!Jp2Reader::canRead(file));
}

BOOST_AUTO_TEST_SUITE_END()
