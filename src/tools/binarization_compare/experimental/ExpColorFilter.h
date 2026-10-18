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


// Taken unchanged from ScanTailor Experimental (commit 80e1c57, imageproc/ColorFilter.h), only the
// declarations of the K-means color reduction, put into the namespace imageproc::stexp.

#pragma once

#include <QImage>
#include "BinaryImage.h"

namespace imageproc {
namespace stexp {

QImage imageHSVcylinder(QImage const& image);

QImage imageHSLcylinder(QImage const& image);

QImage imageYCbCr(QImage const& image);

float pixelDistance(float const h0, float const s0, float const v0, float const h1, float const s1, float const v1);

void paletteHSVcylinderGenerate(double* mean_h0,
                                double* mean_s0,
                                double* mean_v0,
                                int const ncount,
                                float start_value = 128.0f);

void paletteHSVcylinderToHSV(double* mean_h, double* mean_s, int const start, int const stop);

void paletteHSVsaturation(double* mean_s, float const coef_sat, int const start, int const stop);

void paletteYCbCrsaturation(double* mean_cb, double* mean_cr, float const coef_sat, int const start, int const stop);

void paletteHSVnorm(double* mean_v, float const coef_norm, int const start, int const stop);

void paletteHSVtoRGB(double* mean_h, double* mean_s, double* mean_v, int const ncount);

void paletteHSLtoRGB(double* mean_h, double* mean_s, double* mean_l, int const ncount);

void paletteYCbCrtoRGB(double* mean_cb, double* mean_cr, double* mean_cy, int const ncount);

double hsvKMeansInPlace(QImage& dst,
                        QImage const& image,
                        BinaryImage const& mask,
                        BinaryImage const& mask_zones,
                        int const ncount,
                        int start_value = 0,
                        int color_space = 0,
                        float coef_sat = 0.0f,
                        float coef_norm = 0.0f,
                        float coef_bg = 0.0f,
                        bool find_black = false,
                        bool find_white = false);


}  // namespace stexp
}  // namespace imageproc
