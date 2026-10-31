// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Copyright (C) 1995 Spencer Kimball and Peter Mattis
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "GaussBlur.h"

#include <cmath>

#include "Constants.h"
#include "GrayImage.h"

namespace imageproc {
namespace gauss_blur_impl {
void findIirConstants(double* nP, double* nM, double* dP, double* dM, double* bdP, double* bdM, double stdDev) {
  /*  The constants used in the implemenation of a casual sequence
   *  using a 4th order approximation of the gaussian operator
   */

  const double div = std::sqrt(2.0 * constants::PI) * stdDev;
  const double x0 = -1.783 / stdDev;
  const double x1 = -1.723 / stdDev;
  const double x2 = 0.6318 / stdDev;
  const double x3 = 1.997 / stdDev;
  const double x4 = 1.6803 / div;
  const double x5 = 3.735 / div;
  const double x6 = -0.6803 / div;
  const double x7 = -0.2598 / div;

  nP[0] = x4 + x6;
  nP[1] = std::exp(x1) * (x7 * std::sin(x3) - (x6 + 2 * x4) * std::cos(x3))
          + std::exp(x0) * (x5 * std::sin(x2) - (2 * x6 + x4) * std::cos(x2));
  nP[2] = 2 * std::exp(x0 + x1)
              * ((x4 + x6) * std::cos(x3) * std::cos(x2) - x5 * std::cos(x3) * std::sin(x2)
                 - x7 * std::cos(x2) * std::sin(x3))
          + x6 * std::exp(2 * x0) + x4 * std::exp(2 * x1);
  nP[3] = std::exp(x1 + 2 * x0) * (x7 * std::sin(x3) - x6 * std::cos(x3))
          + std::exp(x0 + 2 * x1) * (x5 * std::sin(x2) - x4 * std::cos(x2));
  nP[4] = 0.0;

  dP[0] = 0.0;
  dP[1] = -2 * std::exp(x1) * std::cos(x3) - 2 * std::exp(x0) * std::cos(x2);
  dP[2] = 4 * std::cos(x3) * std::cos(x2) * std::exp(x0 + x1) + std::exp(2 * x1) + std::exp(2 * x0);
  dP[3] = -2 * std::cos(x2) * std::exp(x0 + 2 * x1) - 2 * std::cos(x3) * std::exp(x1 + 2 * x0);
  dP[4] = std::exp(2 * x0 + 2 * x1);

  for (int i = 0; i <= 4; i++) {
    dM[i] = dP[i];
  }

  nM[0] = 0.0;

  for (int i = 1; i <= 4; i++) {
    nM[i] = nP[i] - dP[i] * nP[0];
  }

  double sumNP = 0.0;
  double sumNM = 0.0;
  double sumD = 0.0;

  for (int i = 0; i <= 4; i++) {
    sumNP += nP[i];
    sumNM += nM[i];
    sumD += dP[i];
  }

  const double a = sumNP / (1.0 + sumD);
  const double b = sumNM / (1.0 + sumD);

  for (int i = 0; i <= 4; i++) {
    bdP[i] = dP[i] * a;
    bdM[i] = dM[i] * b;
  }
}  // findIirConstants
}  // namespace gauss_blur_impl

GrayImage gaussBlur(const GrayImage& src, float hSigma, float vSigma) {
  if (src.isNull()) {
    return src;
  }

  GrayImage dst(src.size());
  gaussBlurGeneric(src.size(), hSigma, vSigma, src.data(), src.stride(), StaticCastValueConv<float>(), dst.data(),
                   dst.stride(),
                   [](uint8_t& dst, const float value) { dst = RoundAndClipValueConv<uint8_t>()(value); });
  return dst;
}
}  // namespace imageproc
