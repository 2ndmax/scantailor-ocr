// Copyright (C) 2015  Joseph Artsimovich <joseph.artsimovich@gmail.com>
// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// The binarization methods Gatos, Nick, Singh, Robust and MultiScale, taken over from ScanTailor
// Experimental (https://github.com/ImageProcessing-ElectronicPublications/scantailor-experimental,
// imageproc/Binarize.cpp and imageproc/GrayImage.cpp, commit 80e1c57). The functions in
// namespace experimental compute the same as there, apart from the fixes noted below; they have
// been reformatted and limited to what these methods need. Only the functions at the end are new.
// Fixes: the mean window of Singh (grayMapMean) was one column too narrow, Robust turned pixels
// lighter than twice their mean black (grayRobustInPlace), and the iteration of
// grayBiModalTiledValue has a cap.

#include "ExperimentalBinarization.h"

#include <QRect>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "BinaryImage.h"
#include "GaussBlur.h"
#include "GrayImage.h"
#include "IntegralImage.h"

namespace imageproc {
namespace experimental {
namespace {
void grayWienerInPlace(GrayImage& src, int radius, float noise_sigma);
void grayRobustInPlace(GrayImage& src, int radius, float coef);

GrayImage grayMapMean(GrayImage const& src, int const radius) {
  if (src.isNull()) {
    return GrayImage();
  }
  GrayImage gray = GrayImage(src);
  if (gray.isNull()) {
    return GrayImage();
  }

  if (radius > 0) {
    int const w = src.width();
    int const h = src.height();
    unsigned char const* src_line = src.data();
    int const src_stride = src.stride();
    unsigned char* gray_line = gray.data();
    int const gray_stride = gray.stride();

    IntegralImage<uint32_t> integral_image(w, h);

    for (int y = 0; y < h; y++) {
      integral_image.beginRow();
      for (int x = 0; x < w; x++) {
        uint32_t const pixel = src_line[x];
        integral_image.push(pixel);
      }
      src_line += src_stride;
    }

    for (int y = 0; y < h; y++) {
      int const top = ((y - radius) < 0) ? 0 : (y - radius);
      int const bottom = ((y + radius + 1) < h) ? (y + radius + 1) : h;

      for (int x = 0; x < w; x++) {
        int const left = ((x - radius) < 0) ? 0 : (x - radius);
        int const right = ((x + radius + 1) < w) ? (x + radius + 1) : w;
        int const area = (bottom - top) * (right - left);
        assert(area > 0);  // because windowSize > 0 and w > 0 and h > 0

        QRect const rect(left, top, right - left, bottom - top);
        double const window_sum = integral_image.sum(rect);

        double const r_area = 1.0 / area;
        double mean = window_sum * r_area;

        mean += 0.5;
        mean = (mean < 0.0) ? 0.0 : ((mean < 255.0) ? mean : 255.0);
        gray_line[x] = (unsigned char) mean;
      }
      gray_line += gray_stride;
    }
  }

  return gray;
}  // grayMapMean

GrayImage grayMapDeviation(GrayImage const& src, int const radius) {
  if (src.isNull()) {
    return GrayImage();
  }
  GrayImage gray = GrayImage(src);
  if (gray.isNull()) {
    return GrayImage();
  }

  if (radius > 0) {
    int const w = src.width();
    int const h = src.height();
    unsigned char const* src_line = src.data();
    int const src_stride = src.stride();
    unsigned char* gray_line = gray.data();
    int const gray_stride = gray.stride();

    IntegralImage<uint32_t> integral_image(w, h);
    IntegralImage<uint64_t> integral_sqimage(w, h);

    uint32_t gray_min = 255;

    for (int y = 0; y < h; y++) {
      integral_image.beginRow();
      integral_sqimage.beginRow();
      for (int x = 0; x < w; x++) {
        uint32_t const pixel = src_line[x];
        integral_image.push(pixel);
        integral_sqimage.push(pixel * pixel);
        gray_min = std::min(gray_min, pixel);
      }
      src_line += src_stride;
    }

    for (int y = 0; y < h; y++) {
      int const top = ((y - radius) < 0) ? 0 : (y - radius);
      int const bottom = ((y + radius + 1) < h) ? (y + radius + 1) : h;

      for (int x = 0; x < w; x++) {
        int const left = ((x - radius) < 0) ? 0 : (x - radius);
        int const right = ((x + radius + 1) < w) ? (x + radius + 1) : w;
        int const area = (bottom - top) * (right - left);
        assert(area > 0);  // because window_size > 0 and w > 0 and h > 0

        QRect const rect(left, top, right - left, bottom - top);
        double const window_sum = integral_image.sum(rect);
        double const window_sqsum = integral_sqimage.sum(rect);

        double const r_area = 1.0 / area;
        double const mean = window_sum * r_area;
        double const sqmean = window_sqsum * r_area;

        double const variance = sqmean - mean * mean;
        double deviation = sqrt(fabs(variance));

        deviation += 0.5;
        deviation = (deviation < 0.0) ? 0.0 : ((deviation < 255.0) ? deviation : 255.0);
        gray_line[x] = (unsigned char) deviation;
      }
      gray_line += gray_stride;
    }
  }

  return gray;
}  // grayMapDeviation

unsigned int grayBiModalTiledValue(GrayImage const& src,
                                   unsigned int const x1,
                                   unsigned int const y1,
                                   unsigned int const x2,
                                   unsigned int const y2,
                                   int const delta,
                                   unsigned char const bound_lower,
                                   unsigned char const bound_upper) {
  unsigned int const histsize = 256;
  unsigned int threshold = histsize >> 1;
  if (src.isNull()) {
    return threshold;
  }

  unsigned int const w = src.width();
  unsigned int const h = src.height();
  unsigned char const* src_line = src.data();
  unsigned int const src_stride = src.stride();
  uint64_t im, iw, ib, histogram[histsize] = {0};
  unsigned int k, Tn;
  uint64_t Tw, Tb;
  int Tmin = 255, Tmax = 0, Ti;
  int part = 256 + delta + delta;
  part = (part < 0) ? 0 : (part < 512) ? part : 512;

  unsigned int const x1w = (x1 < w) ? x1 : w;
  unsigned int const y1w = (y1 < h) ? y1 : h;
  unsigned int const x2w = (x2 < w) ? x2 : w;
  unsigned int const y2w = (y2 < h) ? y2 : h;
  uint64_t const wsize = (y2w - y1w) * (x2w - x1w);

  if (wsize > 0) {
    src_line += y1w * src_stride;
    for (unsigned int y = y1w; y < y2w; y++) {
      for (unsigned int x = x1w; x < x2w; x++) {
        unsigned char pixel = src_line[x];
        pixel = (pixel < bound_lower) ? bound_lower : (pixel <= bound_upper) ? pixel : bound_upper;
        histogram[pixel]++;
      }
      src_line += src_stride;
    }

    Ti = 0;
    while (Ti < Tmin) {
      Tmin = (histogram[Ti] > 0) ? Ti : Tmin;
      Ti++;
    }
    Ti = 255;
    while (Ti > Tmax) {
      Tmax = (histogram[Ti] > 0) ? Ti : Tmax;
      Ti--;
    }
    Tmax++;
    Tmin <<= 7;
    Tmax <<= 7;

    threshold = part * Tmax + (512 - part) * Tmin;
    threshold >>= 8;
    Tn = threshold + 1;
    // Integer rounding might let the threshold alternate between two values; the cap ends that.
    for (int iteration = 0; (threshold != Tn) && (iteration < 256); ++iteration) {
      Tn = threshold;
      Tb = Tw = 0;
      ib = iw = 0;
      for (k = 0; k < histsize; k++) {
        im = histogram[k];
        if ((k << 8) < threshold) {
          Tb += im * k;
          ib += im;
        } else {
          Tw += im * k;
          iw += im;
        }
      }
      Tb = (ib > 0) ? ((Tb << 7) / ib) : Tmin;
      Tw = (iw > 0) ? ((Tw << 7) / iw) : Tmax;
      threshold = ((part * Tw + (512 - part) * Tb + 127) >> 8);
    }
    threshold += 127;
    threshold >>= 8;
  }

  return threshold;
}  // grayBiModalTiledValue

GrayImage grayNiblackMap(GrayImage const& src, int const radius, float const k, int const delta) {
  if (src.isNull()) {
    return GrayImage();
  }

  GrayImage gmean(src);
  if (gmean.isNull()) {
    return gmean;
  }

  if (radius > 0) {
    int const w = src.width();
    int const h = src.height();

    unsigned char const* src_line = src.data();
    int const src_stride = src.stride();
    unsigned char* gmean_line = gmean.data();
    int const gmean_stride = gmean.stride();

    IntegralImage<uint32_t> integral_image(w, h);
    IntegralImage<uint64_t> integral_sqimage(w, h);

    for (int y = 0; y < h; y++) {
      integral_image.beginRow();
      integral_sqimage.beginRow();
      for (int x = 0; x < w; x++) {
        uint32_t const pixel = src_line[x];
        integral_image.push(pixel);
        integral_sqimage.push(pixel * pixel);
      }
      src_line += src_stride;
    }

    for (int y = 0; y < h; y++) {
      int const top = ((y - radius) < 0) ? 0 : (y - radius);
      int const bottom = ((y + radius + 1) < h) ? (y + radius + 1) : h;

      for (int x = 0; x < w; x++) {
        int const left = ((x - radius) < 0) ? 0 : (x - radius);
        int const right = ((x + radius + 1) < w) ? (x + radius + 1) : w;
        int const area = (bottom - top) * (right - left);
        assert(area > 0);  // because window_size > 0 and w > 0 and h > 0

        QRect const rect(left, top, right - left, bottom - top);
        double const window_sum = integral_image.sum(rect);
        double const window_sqsum = integral_sqimage.sum(rect);

        double const r_area = 1.0 / area;
        double const mean = window_sum * r_area;
        double const sqmean = window_sqsum * r_area;

        double const variance = sqmean - mean * mean;
        double const deviation = sqrt(fabs(variance));

        double const shift = deviation - delta;
        double threshold = mean - k * shift;

        threshold = (threshold < 0.0) ? 0.0 : ((threshold < 255.0) ? threshold : 255.0);
        gmean_line[x] = (unsigned char) threshold;
      }
      gmean_line += gmean_stride;
    }
  }

  return gmean;
}  // grayNiblackMap

void grayBGtoMap(GrayImage const& src, GrayImage& background, float const q, float const p) {
  if (src.isNull() || background.isNull()) {
    return;
  }

  int const w = src.width();
  int const h = src.height();

  if ((w != background.width()) || (h != background.height())) {
    return;
  }
  unsigned char const* src_line = src.data();
  int const src_stride = src.stride();
  unsigned char* background_line = background.data();
  int const background_stride = background.stride();
  int const hist_size = 256;
  unsigned char histogram[hist_size] = {0};

  // sum(background - original) for foreground pixels (background > original).
  uint64_t sum_diff = 0;
  // sum(background) for background pixels (background <= original).
  uint64_t sum_bg = 0;
  uint64_t sum_contour = 0;
  uint64_t src_size = w * h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int const im = src_line[x];
      int const bg = background_line[x];
      if (im < bg) {
        sum_diff += (bg - im);
        sum_contour++;
      } else {
        sum_bg += bg;
      }
    }
    src_line += src_stride;
    background_line += background_stride;
  }

  if ((sum_contour == 0) || (sum_contour == src_size)) {
    return;
  }

  float const d = ((float) sum_diff) / sum_contour;
  float const b = ((float) sum_bg) / (src_size - sum_contour);

  float const qd = q * d;
  float const threshold_scale = qd * p;
  float const threshold_bias = qd * (1.0f - p);

  for (int k = 0; k < hist_size; k++) {
    float const bgf = ((float) k + 1.0f) / (b + 1.0f);
    float const tk = 1.0f / (1.0f + expf(-8.0f * bgf + 6.0f));
    int const threshold = (threshold_scale * tk + threshold_bias + 0.5f);
    int bgn = k - threshold;
    bgn = (bgn < 0) ? 0 : ((bgn < 255) ? bgn : 255);
    histogram[k] = (unsigned char) bgn;
  }

  background_line = background.data();
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char const bg = background_line[x];
      unsigned char const bgn = histogram[bg];
      background_line[x] = bgn;
    }
    background_line += background_stride;
  }
}

GrayImage grayNickMap(GrayImage const& src, int const radius, float const k, int const delta) {
  if (src.isNull()) {
    return GrayImage();
  }

  GrayImage gmean(src);
  if (gmean.isNull()) {
    return gmean;
  }

  if (radius > 0) {
    double cnick = (50.0 - delta) * 0.01;

    int const w = src.width();
    int const h = src.height();

    unsigned char const* src_line = src.data();
    int const src_stride = src.stride();
    unsigned char* gmean_line = gmean.data();
    int const gmean_stride = gmean.stride();

    IntegralImage<uint32_t> integral_image(w, h);
    IntegralImage<uint64_t> integral_sqimage(w, h);

    for (int y = 0; y < h; y++) {
      integral_image.beginRow();
      integral_sqimage.beginRow();
      for (int x = 0; x < w; x++) {
        uint32_t const pixel = src_line[x];
        integral_image.push(pixel);
        integral_sqimage.push(pixel * pixel);
      }
      src_line += src_stride;
    }

    for (int y = 0; y < h; y++) {
      int const top = ((y - radius) < 0) ? 0 : (y - radius);
      int const bottom = ((y + radius + 1) < h) ? (y + radius + 1) : h;

      for (int x = 0; x < w; x++) {
        int const left = ((x - radius) < 0) ? 0 : (x - radius);
        int const right = ((x + radius + 1) < w) ? (x + radius + 1) : w;
        int const area = (bottom - top) * (right - left);
        assert(area > 0);  // because window_size > 0 and w > 0 and h > 0

        QRect const rect(left, top, right - left, bottom - top);
        double const window_sum = integral_image.sum(rect);
        double const window_sqsum = integral_sqimage.sum(rect);

        double const r_area = 1.0 / area;
        double const mean = window_sum * r_area;
        double const sqmean = window_sqsum * r_area;

        double const variance = sqmean - mean * mean;
        double const deviation = sqrt(fabs(variance));

        double const circle = sqrt(deviation * deviation + cnick * mean * mean);
        double threshold = mean - k * circle;

        threshold = (threshold < 0.0) ? 0.0 : ((threshold < 255.0) ? threshold : 255.0);
        gmean_line[x] = (unsigned char) threshold;
      }
      gmean_line += gmean_stride;
    }
  }

  return gmean;
}  // grayNickMap

GrayImage graySinghMap(GrayImage const& src, int const radius, float const k, int const delta) {
  if (src.isNull()) {
    return GrayImage();
  }
  GrayImage gmean = grayMapMean(src, radius);
  if (gmean.isNull()) {
    return GrayImage(src);
  }

  if (radius > 0) {
    int const w = src.width();
    int const h = src.height();
    unsigned char const* src_line = src.data();
    int const src_stride = src.stride();
    unsigned char* gmean_line = gmean.data();
    int const gmean_stride = gmean.stride();

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float const origin = src_line[x];
        float const mean = gmean_line[x];

        float const grad = origin - mean;
        float const frac = grad / (256.0f - grad);
        float const frac_delta = (float) delta / 128.0f;
        float const part = 0.5f * (1.0f - (frac + frac_delta));
        float threshold = mean * (1.0f - k * part);

        threshold = (threshold < 0.0f) ? 0.0f : ((threshold < 255.0f) ? threshold : 255.0f);
        gmean_line[x] = (unsigned char) threshold;
      }
      src_line += src_stride;
      gmean_line += gmean_stride;
    }
  }

  return gmean;
}  // graySinghMap

GrayImage grayMScaleMap(GrayImage const& src, int const radius, float const coef) {
  if (src.isNull()) {
    return GrayImage();
  }
  GrayImage gray = GrayImage(src);
  if (gray.isNull()) {
    return GrayImage();
  }
  if (radius < 1) {
    return gray;
  }

  int const w = src.width();
  int const h = src.height();
  unsigned char const* src_line = src.data();
  int const src_stride = src.stride();
  unsigned char* gray_line = gray.data();
  int const gray_stride = gray.stride();

  unsigned int whcp, l, i, j, blsz, rsz;
  float immean, imt, kover, sensitivity, sensdiv, senspos, sensinv;
  unsigned int pim, immin, immax, cnth, cntw, level = 0;
  unsigned int maskbl, maskover, tim;
  unsigned long int idx;

  whcp = (h + w) >> 1;
  blsz = 1;
  while (blsz < whcp) {
    level++;
    blsz <<= 1;
  }
  blsz >>= 1;
  rsz = 1;
  while (((int) rsz < radius) && (level > 1)) {
    level--;
    rsz <<= 1;
  }

  gray_line = gray.data();
  immin = gray_line[0];
  immax = immin;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      pim = gray_line[x];
      if (pim < immin) {
        immin = pim;
      }
      if (pim > immax) {
        immax = pim;
      }
    }
    gray_line += gray_stride;
  }
  immean = (float) (immax + immin);
  immean *= 0.5f;
  immean += 0.5f;
  tim = (unsigned int) immean;

  gray_line = gray.data();
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      gray_line[x] = tim;
    }
    gray_line += gray_stride;
  }

  kover = 1.5f;

  if (coef < 0.0f) {
    sensitivity = -coef;
    sensdiv = sensitivity;
    sensdiv += 1.0f;
    sensinv = 1.0f / sensdiv;
    senspos = sensitivity / sensdiv;
  } else {
    sensitivity = coef;
    sensdiv = sensitivity;
    sensdiv += 1.0f;
    senspos = 1.0f / sensdiv;
    sensinv = sensitivity / sensdiv;
  }

  src_line = src.data();
  gray_line = gray.data();
  for (l = 0; l < level; l++) {
    cnth = (h + blsz - 1) / blsz;
    cntw = (w + blsz - 1) / blsz;
    maskbl = blsz;
    maskover = (unsigned int) (kover * maskbl);
    for (i = 0; i < cnth; i++) {
      int y0 = i * maskbl;
      int y1 = y0 + maskover;
      y1 = (y1 < h) ? y1 : h;
      for (j = 0; j < cntw; j++) {
        int x0 = j * maskbl;
        int x1 = x0 + maskover;
        x1 = (x1 < w) ? x1 : w;

        idx = y0 * src_stride + x0;
        immin = src_line[idx];
        immax = immin;
        for (int y = y0; y < y1; y++) {
          for (int x = x0; x < x1; x++) {
            idx = y * src_stride + x;
            pim = src_line[idx];
            if (pim < immin) {
              immin = pim;
            }
            if (pim > immax) {
              immax = pim;
            }
          }
        }
        immean = (float) (immax + immin);
        immean *= 0.5f;
        immean *= sensinv;
        for (int y = y0; y < y1; y++) {
          for (int x = x0; x < x1; x++) {
            idx = y * gray_stride + x;
            imt = gray_line[idx];
            imt *= senspos;
            imt += immean;
            imt += 0.5f;
            imt = (imt < 0.0f) ? 0.0f : ((imt < 255.0f) ? imt : 255.0f);
            gray_line[idx] = (unsigned char) imt;
          }
        }
      }
    }
    blsz >>= 1;
  }

  return gray;
}  // grayMScaleMap

GrayImage grayWiener(GrayImage const& src, int const radius, float const noise_sigma) {
  GrayImage dst(src);
  grayWienerInPlace(dst, radius, noise_sigma);
  return dst;
}

void grayWienerInPlace(GrayImage& src, int const radius, float const noise_sigma) {
  if (src.isNull()) {
    return;
  }

  if ((radius > 0) && (noise_sigma > 0.0f)) {
    int const w = src.width();
    int const h = src.height();
    float const noise_variance = noise_sigma * noise_sigma;

    unsigned char* src_line = src.data();
    int const src_stride = src.stride();

    GrayImage gmean = gaussBlur(src, radius, radius);
    if (gmean.isNull()) {
      return;
    }

    unsigned char* gmean_line = gmean.data();
    int const gmean_stride = gmean.stride();

    GrayImage gdeviation = grayMapDeviation(src, radius);
    if (gdeviation.isNull()) {
      return;
    }

    unsigned char* gdeviation_line = gdeviation.data();
    int const gdeviation_stride = gdeviation.stride();

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float const mean = gmean_line[x];
        float const deviation = gdeviation_line[x];
        float const variance = deviation * deviation;

        float const src_pixel = (float) src_line[x];
        float const delta_pixel = src_pixel - mean;
        float const delta_variance = variance - noise_variance;
        float dst_pixel = mean;
        if (delta_variance > 0.0f) {
          dst_pixel += delta_pixel * delta_variance / variance;
        }
        int val = (int) (dst_pixel + 0.5f);
        val = (val < 0) ? 0 : (val < 255) ? val : 255;
        src_line[x] = (unsigned char) val;
      }
      src_line += src_stride;
      gmean_line += gmean_stride;
      gdeviation_line += gdeviation_stride;
    }
  }
}  // grayWienerInPlace

GrayImage grayRobust(GrayImage const& src, int const radius, float const coef) {
  GrayImage dst(src);
  grayRobustInPlace(dst, radius, coef);
  return dst;
}

void grayRobustInPlace(GrayImage& src, int const radius, float const coef) {
  if (src.isNull()) {
    return;
  }

  if ((radius > 0) && (coef != 0.0f)) {
    int const w = src.width();
    int const h = src.height();
    unsigned char* src_line = src.data();
    int const src_stride = src.stride();

    GrayImage gmean = gaussBlur(src, radius, radius);
    if (gmean.isNull()) {
      return;
    }
    unsigned char* gmean_line = gmean.data();
    int const gmean_stride = gmean.stride();

    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        float const origin = src_line[x];
        float const mean = gmean_line[x];
        float retval = origin;
        float const sc = mean - origin;
        if (mean + sc <= 0.0f) {
          // At twice the mean the formula below has a pole, beyond it it turns light pixels black,
          // and a black area (origin = mean = 0) gives 0/0. Such pixels are white, the value
          // the formula approaches just below the pole.
          src_line[x] = 255;
          continue;
        }
        float const robust = 255.0f - (mean + 255.0f) * sc / (mean + sc);
        retval = coef * robust + (1.0f - coef) * origin + 0.5f;
        retval = (retval < 0.0f) ? 0.0f : (retval < 255.0f) ? retval : 255.0f;
        src_line[x] = (unsigned char) retval;
      }
      src_line += src_stride;
      gmean_line += gmean_stride;
    }
  }
}  // grayRobustInPlace

static inline bool binaryGetBW(uint32_t const* bw_line, unsigned int x) {
  static uint32_t const msb = uint32_t(1) << 31;
  uint32_t const mask = msb >> (x & 31);

  return (bw_line[x >> 5] & mask);
}

static inline void binarySetBW(uint32_t* bw_line, unsigned int x, bool black) {
  static uint32_t const msb = uint32_t(1) << 31;
  uint32_t const mask = msb >> (x & 31);
  if (black) {
    // black
    bw_line[x >> 5] |= mask;
  } else {
    // white
    bw_line[x >> 5] &= ~mask;
  }
}

BinaryImage binarizeUse(GrayImage const& src, unsigned int const threshold) {
  if (src.isNull()) {
    return BinaryImage();
  }

  unsigned int const w = src.width();
  unsigned int const h = src.height();
  uint8_t const* src_line = src.data();
  unsigned int const src_stride = src.stride();

  BinaryImage bw_img(w, h);
  if (bw_img.isNull()) {
    return BinaryImage();
  }

  uint32_t* bw_line = bw_img.data();
  unsigned int const bw_stride = bw_img.wordsPerLine();

  for (unsigned int y = 0; y < h; y++) {
    for (unsigned int x = 0; x < w; x++) {
      binarySetBW(bw_line, x, (src_line[x] < threshold));
    }
    src_line += src_stride;
    bw_line += bw_stride;
  }

  return bw_img;
}  // binarizeUse

BinaryImage binarizeFromMap(GrayImage const& src,
                            GrayImage const& threshold,
                            int const delta,
                            unsigned char const bound_lower,
                            unsigned char const bound_upper) {
  if (src.isNull() || threshold.isNull()) {
    return BinaryImage();
  }

  unsigned int const w = src.width();
  unsigned int const h = src.height();
  unsigned int const wt = threshold.width();
  unsigned int const ht = threshold.height();

  if ((w != wt) || (h != ht)) {
    return BinaryImage();
  }

  uint8_t const* src_line = src.data();
  unsigned int const src_stride = src.stride();
  uint8_t const* threshold_line = threshold.data();
  unsigned int const threshold_stride = threshold.stride();

  BinaryImage bw_img(w, h);
  if (bw_img.isNull()) {
    return BinaryImage();
  }

  uint32_t* bw_line = bw_img.data();
  unsigned int const bw_stride = bw_img.wordsPerLine();

  for (unsigned int y = 0; y < h; y++) {
    for (unsigned int x = 0; x < w; x++) {
      binarySetBW(bw_line, x,
                  (src_line[x] < bound_lower
                   || (src_line[x] <= bound_upper && ((int) src_line[x] < ((int) threshold_line[x] + delta)))));
    }
    src_line += src_stride;
    threshold_line += threshold_stride;
    bw_line += bw_stride;
  }

  return bw_img;
}  // binarizeFromMap

unsigned int binarizeBiModalValue(GrayImage const& src,
                                  int const delta,
                                  unsigned char const bound_lower,
                                  unsigned char const bound_upper) {
  unsigned int threshold = 128;
  if (src.isNull()) {
    return threshold;
  }

  unsigned int const w = src.width();
  unsigned int const h = src.height();
  threshold = grayBiModalTiledValue(src, 0, 0, w, h, delta, bound_lower, bound_upper);

  return threshold;
}  // binarizeBiModalValue

BinaryImage binarizeBiModal(GrayImage const& src,
                            int const delta,
                            unsigned char const bound_lower,
                            unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  unsigned int threshold = binarizeBiModalValue(src, delta, bound_lower, bound_upper);
  BinaryImage bw_img = binarizeUse(src, threshold);

  return bw_img;
}  // binarizeBiModal

BinaryImage binarizeNiblack(GrayImage const& src,
                            int const radius,
                            float const k,
                            int const delta,
                            unsigned char const bound_lower,
                            unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage threshold_map(grayNiblackMap(src, radius, k, delta));
  BinaryImage bw_img(binarizeFromMap(src, threshold_map, 0, bound_lower, bound_upper));

  return bw_img;
}

GrayImage binarizeGatosBG(GrayImage const& src, BinaryImage const& niblack, int const radius) {
  GrayImage gray = GrayImage(src);
  if (gray.isNull() || niblack.isNull()) {
    return GrayImage();
  }
  if (radius < 1) {
    return gray;
  }

  int const w = src.width();
  int const h = src.height();
  unsigned int const ws = radius + radius + 1;

  if ((w != niblack.width()) || (h != niblack.height())) {
    return gray;
  }

  IntegralImage<uint32_t> niblack_bg_ii(w, h);
  IntegralImage<uint32_t> gray_bg_ii(w, h);

  uint32_t const* niblack_line = niblack.data();
  int const niblack_stride = niblack.wordsPerLine();
  uint8_t const* gray_line = gray.data();
  int const gray_stride = gray.stride();

  for (int y = 0; y < h; y++) {
    niblack_bg_ii.beginRow();
    gray_bg_ii.beginRow();
    for (int x = 0; x < w; x++) {
      // bg: 1, fg: 0
      uint32_t const niblack_inverted_pixel = (~niblack_line[x >> 5] >> (31 - (x & 31))) & uint32_t(1);
      uint32_t const gray_pixel = gray_line[x];
      niblack_bg_ii.push(niblack_inverted_pixel);

      // bg: gray_pixel, fg: 0
      gray_bg_ii.push(gray_pixel & ~(niblack_inverted_pixel - uint32_t(1)));
    }
    gray_line += gray_stride;
    niblack_line += niblack_stride;
  }

  GrayImage background(gray);
  if (background.isNull()) {
    return gray;
  }
  uint8_t* background_line = background.data();
  int const background_stride = background.stride();

  QRect const image_rect(gray.rect());

  niblack_line = niblack.data();
  unsigned int const w2 = w + w;
  unsigned int const h2 = h + h;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      if (binaryGetBW(niblack_line, x)) {
        uint32_t niblack_sum_bg = 0;
        unsigned int wss = 0;
        QRect window;
        while ((niblack_sum_bg == 0) && ((wss < w2) || (wss < h2))) {
          wss += ws;
          window = QRect(0, 0, wss, wss);
          window.moveCenter(QPoint(x, y));
          window &= image_rect;
          niblack_sum_bg = niblack_bg_ii.sum(window);
        }

        // Foreground pixel. Interpolate from background pixels in window.
        uint32_t bg = background_line[x];
        if (niblack_sum_bg > 0) {
          uint32_t const gray_sum_bg = gray_bg_ii.sum(window);
          bg = (gray_sum_bg + (niblack_sum_bg >> 1)) / niblack_sum_bg;
        }
        background_line[x] = bg;
      }
    }
    background_line += background_stride;
    niblack_line += niblack_stride;
  }

  return background;
}

BinaryImage binarizeGatos(GrayImage const& src,
                          int const radius,
                          float const noise_sigma,
                          float const k,
                          int const delta,
                          unsigned char const bound_lower,
                          unsigned char const bound_upper,
                          float const q,
                          float const p) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage const wiener(grayWiener(src, 5, noise_sigma));
  BinaryImage niblack(binarizeNiblack(wiener, radius, k, 0, bound_lower, bound_upper));
  GrayImage threshold_map(binarizeGatosBG(wiener, niblack, radius));
  float const qd = q - (1.0f - q) * delta * 0.02f;
  grayBGtoMap(wiener, threshold_map, qd, p);
  BinaryImage bw_img(binarizeFromMap(wiener, threshold_map, 0, bound_lower, bound_upper));

  return bw_img;
}

BinaryImage binarizeNick(GrayImage const& src,
                         int const radius,
                         float const k,
                         int const delta,
                         unsigned char const bound_lower,
                         unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage threshold_map(grayNickMap(src, radius, k, delta));
  BinaryImage bw_img(binarizeFromMap(src, threshold_map, 0, bound_lower, bound_upper));

  return bw_img;
}

BinaryImage binarizeSingh(GrayImage const& src,
                          int const radius,
                          float const k,
                          int const delta,
                          unsigned char const bound_lower,
                          unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage threshold_map(graySinghMap(src, radius, k, delta));
  BinaryImage bw_img(binarizeFromMap(src, threshold_map, 0, bound_lower, bound_upper));

  return bw_img;
}  // binarizeSingh

BinaryImage binarizeRobust(GrayImage const& src,
                           int const radius,
                           float const k,
                           int const delta,
                           unsigned char const bound_lower,
                           unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage gray(grayRobust(src, radius, k));
  BinaryImage bw_img(binarizeBiModal(gray, delta, bound_lower, bound_upper));

  return bw_img;
}  // binarizeRobust

BinaryImage binarizeMScale(GrayImage const& src,
                           int const radius,
                           float const coef,
                           int const delta,
                           unsigned char const bound_lower,
                           unsigned char const bound_upper) {
  if (src.isNull()) {
    return BinaryImage();
  }

  GrayImage threshold_map(grayMScaleMap(src, radius, coef));
  BinaryImage bw_img(binarizeFromMap(src, threshold_map, delta, bound_lower, bound_upper));

  return bw_img;
}  // binarizeMScale

}  // namespace
}  // namespace experimental

namespace {
// The window size of the options panel covers 2 * radius + 1 pixels.
int radiusOf(const int windowSize) {
  return std::max(1, windowSize / 2);
}
}  // namespace

BinaryImage binarizeGatos(const GrayImage& src, const int windowSize, const double k, const int delta) {
  return experimental::binarizeGatos(src, radiusOf(windowSize), 3.0f, static_cast<float>(k), delta, 0, 255, 0.6f, 0.2f);
}

BinaryImage binarizeNick(const GrayImage& src, const int windowSize, const double k, const int delta) {
  return experimental::binarizeNick(src, radiusOf(windowSize), static_cast<float>(k), delta, 0, 255);
}

BinaryImage binarizeSingh(const GrayImage& src, const int windowSize, const double k, const int delta) {
  return experimental::binarizeSingh(src, radiusOf(windowSize), static_cast<float>(k), delta, 0, 255);
}

BinaryImage binarizeRobust(const GrayImage& src, const int windowSize, const double k, const int delta) {
  return experimental::binarizeRobust(src, radiusOf(windowSize), static_cast<float>(k), delta, 0, 255);
}

BinaryImage binarizeMultiScale(const GrayImage& src, const int windowSize, const double k, const int delta) {
  return experimental::binarizeMScale(src, radiusOf(windowSize), static_cast<float>(k), delta, 0, 255);
}
}  // namespace imageproc
