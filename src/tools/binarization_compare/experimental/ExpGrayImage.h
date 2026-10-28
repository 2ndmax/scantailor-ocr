/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2015  Joseph Artsimovich <joseph.artsimovich@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/


// Taken unchanged from ScanTailor Experimental (commit 80e1c57), only put into the
// namespace imageproc::stexp, for comparing binarization methods. GPLv3, as the original.
// Analyzer findings in this copy (dead stores, divisions the analyzer can't prove non-zero,
// etc.) are kept on purpose, so that it stays comparable with the original.

#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <cmath>
#include <cstdint>
#include "BinaryImage.h"
#include "GaussBlur.h"
#include "GrayImage.h"
#include "IntegralImage.h"

namespace imageproc {
namespace stexp {

/**
 * \brief Statistic Images:
 * mean, deviation, max, contrast
 */
GrayImage grayMapMean(GrayImage const& src, int radius = 100);
GrayImage grayMapDeviation(GrayImage const& src, int radius = 100);
GrayImage grayMapMax(GrayImage const& src, int radius = 100);
GrayImage grayMapContrast(GrayImage const& src, int radius = 100);

/**
 * \brief Threshold Map Images:
 * https://github.com/brandonmpetty/Doxa
 * https://habr.com/ru/articles/907996/
 */
unsigned int grayBiModalTiledValue(GrayImage const& src,
                                   unsigned int const x1,
                                   unsigned int const y1,
                                   unsigned int const x2,
                                   unsigned int const y2,
                                   int delta = 0,
                                   unsigned char bound_lower = 0,
                                   unsigned char bound_upper = 255);
GrayImage grayBiModalTiledMap(GrayImage const& src,
                              int radius = 50,
                              float k = 0.75f,
                              int delta = 0,
                              unsigned char bound_lower = 0,
                              unsigned char bound_upper = 255);
GrayImage grayNiblackMap(GrayImage const& src, int radius = 100, float k = 0.20f, int delta = 0);
void grayBGtoMap(GrayImage const& src, GrayImage& background, float q = 0.6f, float p = 0.20f);
GrayImage graySauvolaMap(GrayImage const& src, int radius = 100, float k = 0.30f, int delta = 0);
GrayImage grayWolfMap(GrayImage const& src, int radius = 100, float k = 0.30f, int delta = 0);
GrayImage grayWindowMap(GrayImage const& src, int radius = 50, float k = 1.0f, int delta = 0);
GrayImage grayLCaMMap(GrayImage const& src, int radius = 5, float k = 0.75f, int delta = 0);
GrayImage grayBradleyMap(GrayImage const& src, int radius = 100, float k = 0.20f);
GrayImage grayNickMap(GrayImage const& src, int radius = 100, float k = 0.10f, int delta = 0);
GrayImage grayGradMap(GrayImage const& src, int radius = 10, float coef = 0.75f, int delta = 0);
GrayImage graySinghMap(GrayImage const& src, int radius = 100, float k = 0.30f, int delta = 0);
GrayImage grayFoxMap(GrayImage const& src, int radius = 100, float k = 0.30f, int delta = 0);
GrayImage grayWANMap(GrayImage const& src, int radius = 100, float k = 0.30f, int delta = 0);
GrayImage grayOBBPMMap(GrayImage const& src, int radius = 100, float k = 0.10f, int delta = 0);
GrayImage grayMScaleMap(GrayImage const& src, int radius = 10, float coef = 0.50f);
GrayImage grayEngravingMap(GrayImage const& src, int radius = 20, float coef = 0.20f);
GrayImage grayDotsMap(GrayImage const& src, int delta = 0);

/**
 * threshold MAPs as filters
 */
void grayMapOverlay(GrayImage& src, GrayImage& gover, float coef = 0.0f);

void grayMix(GrayImage& src, GrayImage& gover, float coef = 0.0f);

void grayGrainExtract(GrayImage& src, GrayImage& gover, float coef = 0.0f);

/**
 * @brief S-curve.
 *
 * (0-T)+(T-255), T = mean(I)
 */
GrayImage grayCurveFilter(GrayImage& src, float coef = 0.5f);

void grayCurveFilterInPlace(GrayImage& src, float coef = 0.5f);

/**
 * @brief C-curve.
 *
 * (0-255)
 */
GrayImage graySqrFilter(GrayImage& src, float coef = 0.0f);

void graySqrFilterInPlace(GrayImage& src, float coef = 0.0f);

/**
 * @brief Engraving GrayImage based GaussBlur.
 */
GrayImage grayGravure(GrayImage const& src, int radius = 15, float coef = 0.0f);
void grayGravureInPlace(GrayImage& src, int radius = 15, float coef = 0.0f);

/**
 * @brief Dots 8x8 GrayImage.
 * Modification: used GaussBlur.
 */
GrayImage grayDots8(GrayImage const& src, int radius = 17, float coef = 0.0f);
void grayDots8InPlace(GrayImage& src, int radius = 17, float coef = 0.0f);

/**
 * @brief RIS undefect GrayImage based linear scale.
 * https://habr.com/ru/articles/910412/
 */
GrayImage grayScaleByLine(GrayImage& src, int const wnew, int const hnew);
GrayImage grayRISundefect(GrayImage const& src, float radius = 2.0f, float coef = 0.0f);
void grayRISundefectInPlace(GrayImage& src, float radius = 2.0f, float coef = 0.0f);

/**
 * @brief Applies the Wiener filter to a grayscale image.
 *
 * @param src The image to apply the filter to. A null image is allowed.
 * @param window_size The local neighbourhood around a pixel to use.
 * @param noise_sigma The standard deviation of noise in the image.
 * @return The filtered image.
 */
GrayImage grayWiener(GrayImage const& src, int radius = 3, float noise_sigma = 0.01f);

/**
 * @brief An in-place version of grayWiener().
 * @see grayWiener()
 */
void grayWienerInPlace(GrayImage& src, int radius = 3, float noise_sigma = 0.01f);

/**
 * @brief KNN denoiser GrayImage based BoxBlur.
 */
GrayImage grayKnnDenoiser(GrayImage const& src, int radius = 7, float coef = 0.0f);
void grayKnnDenoiserInPlace(GrayImage& src, int radius = 7, float coef = 0.0f);

/**
 * @brief EM (Edge Mask) denoiser GrayImage based BoxBlur.
 * https://habr.com/ru/articles/911588/
 */
GrayImage grayEMDenoiser(GrayImage const& src, int radius = 9, float coef = 0.0f);
void grayEMDenoiserInPlace(GrayImage& src, int radius = 9, float coef = 0.0f);

/**
 * @brief Median GrayImage.
 */
GrayImage grayMedian(GrayImage const& src, int radius = 2, float coef = 0.0f);
void grayMedianInPlace(GrayImage& src, int radius = 2, float coef = 0.0f);

/**
 * @brief Subtract BG GrayImage.
 */
GrayImage graySubtractBG(GrayImage const& src, int radius = 45, float coef = 0.0f);
void graySubtractBGInPlace(GrayImage& src, int radius = 45, float coef = 0.0f);

/**
 * @brief Despeckle GrayImage.
 */
GrayImage grayDespeckle(GrayImage const& src, int radius = 7, float coef = 0.0f);
void grayDespeckleInPlace(GrayImage& src, int radius = 7, float coef = 0.0f);

/**
 * @brief Auto Level GrayImage.
 * Modification: used GaussBlur.
 */
GrayImage grayAutoLevel(GrayImage const& src, int radius = 10, float coef = 0.0f);
void grayAutoLevelInPlace(GrayImage& src, int radius = 10, float coef = 0.0f);

/**
 * @brief Balance GrayImage based GaussBlur.
 * https://habr.com/ru/articles/909146/
 */
GrayImage grayBalance(GrayImage const& src, int radius = 23, float coef = 0.0f);
void grayBalanceInPlace(GrayImage& src, int radius = 23, float coef = 0.0f);

/**
 * @brief deBlur GrayImage based GaussBlur.
 * https://habr.com/ru/articles/983796/
 */
GrayImage grayDeBlur5(GrayImage const& src, int radius = 3, float coef = 0.0f);
void grayDeBlur5InPlace(GrayImage& src, int radius = 3, float coef = 0.0f);

/**
 * @brief OverBlur GrayImage based GaussBlur.
 */
GrayImage grayOverBlur(GrayImage const& src, int radius = 49, float coef = 0.0f);
void grayOverBlurInPlace(GrayImage& src, int radius = 49, float coef = 0.0f);

/**
 * @brief Retinex GrayImage based GaussBlur.
 */
GrayImage grayRetinex(GrayImage const& src, int radius = 31, float coef = 0.0f);
void grayRetinexInPlace(GrayImage& src, int radius = 31, float coef = 0.0f);

/**
 * @brief Equalize GrayImage.
 * Modification: used GaussBlur.
 */
GrayImage grayEqualize(GrayImage const& src, int radius = 1, float coef = 0.0f, bool flg_blur = false);
void grayEqualizeInPlace(GrayImage& src, int radius = 1, float coef = 0.0f, bool flg_blur = false);

/**
 * @brief Sigma (blur manipulation) GrayImage based GaussBlur.
 */
GrayImage graySigma(GrayImage const& src, int radius = 29, float coef = 0.0f);
void graySigmaInPlace(GrayImage& src, int radius = 29, float coef = 0.0f);

/**
 * @brief Blur GrayImage based GaussBlur.
 */
GrayImage grayBlur(GrayImage const& src, int radius = 1, float coef = 0.0f);
void grayBlurInPlace(GrayImage& src, int radius = 1, float coef = 0.0f);

/**
 * @brief Comix filter GrayImage based GaussBlur.
 */
GrayImage grayComix(GrayImage const& src, int radius = 6, float coef = 0.0f);
void grayComixInPlace(GrayImage& src, int radius = 6, float coef = 0.0f);

/**
 * @brief Screen GrayImage based GaussBlur.
 */
GrayImage grayScreen(GrayImage const& src, int radius = 5, float coef = 0.0f);
void grayScreenInPlace(GrayImage& src, int radius = 5, float coef = 0.0f);

/**
 * \brief Image prefilter EdgeDiv (EdgePlus & BlurDiv) for local/global thresholding method.
 *
 * EdgeDiv, zvezdochiot 2023. "Adaptive/global document image binarization".
 */
GrayImage grayEdgeDiv(GrayImage const& src, int radius = 10, double kep = 0.5, double kdb = 0.5);
void grayEdgeDivInPlace(GrayImage& src, int radius = 10, double kep = 0.5, double kdb = 0.5);
/**
 * \brief Image prefilter Robust's for local thresholding method.
 *
 * Tom Liao
 * "Robust document binarization with OFF center-surround cells", 2017-08-09.
 * https://www.researchgate.net/publication/226333284_Robust_document_binarization_with_OFF_center-surround_cells
 */
GrayImage grayRobust(GrayImage const& src, int radius = 10, float coef = 0.2f);
void grayRobustInPlace(GrayImage& src, int radius = 10, float coef = 0.2f);

/**
 * \brief Image prefilter Grain for local/global thresholding method.
 *
 * Grain, zvezdochiot 2025. "Adaptive/global document image binarization".
 */
GrayImage grayGrain(GrayImage const& src, int radius = 15, float coef = 0.5f);
void grayGrainInPlace(GrayImage& src, int radius = 15, float coef = 0.5f);

/**
 * @brief Dominanta GrayImage.
 */
unsigned int grayDominantaValue(GrayImage const& src);
}  // namespace stexp
}  // namespace imageproc
