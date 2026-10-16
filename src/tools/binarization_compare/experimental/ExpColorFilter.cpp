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


// Taken from ScanTailor Experimental (commit 80e1c57, imageproc/ColorFilter.cpp: the
// color space conversions, the palette functions and hsvKMeansInPlace), only put into the
// namespace imageproc::stexp, for comparing K-means color reduction.  GPLv3, as the original.
// Fixes in hsvKMeansInPlace: sorting the clusters read the first row of the mask for every row,
// and the background clusters also took foreground pixels.

#define _USE_MATH_DEFINES

#include <math.h>
#include <string.h>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <QImage>
#include "BinaryImage.h"
#include "GrayImage.h"
#include "ExpColorFilter.h"

namespace imageproc
{
namespace stexp
{

QImage imageHSVcylinder(QImage const& image)
{
    if (image.isNull())
    {
        return image;
    }

    unsigned int const w = image.width();
    unsigned int const h = image.height();

    QImage hsv_img(w, h, QImage::Format_RGB32);
    if (hsv_img.isNull())
    {
        return QImage();
    }

    float ctorad = (float)(2.0 * M_PI / 256.0);

    for (unsigned int y = 0; y < h; y++)
    {
        QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
        for (unsigned int x = 0; x < w; x++)
        {
            QRgb pixel = image.pixel(x, y);
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            float hsv_h = 0.0f, hsv_s = 0.0f, hsv_v = 0.0f;
            int max = 0, min = 255;
            max = (r < g) ? g : r;
            max = (max < b) ? b : max;
            min = (r > g) ? g : r;
            min = (min > b) ? b : min;
            hsv_h = max - min;
            if (hsv_h > 0.0f)
            {
                if (max == r)
                {
                    hsv_h = (256.0f * (g - b) / hsv_h) / 6.0f;
                    if (hsv_h < 0.0f)
                    {
                        hsv_h += 256.0f;
                    }
                }
                else if (max == g)
                {
                    hsv_h = (256.0f * (2.0f + (float) (b - r) / hsv_h)) / 6.0f;
                }
                else
                {
                    hsv_h = (256.0f * (4.0f + (float) (r - g) / hsv_h)) / 6.0f;
                }
            }
            hsv_s = max - min;
            if (max > 0)
            {
                hsv_s *= 256.0f;
                hsv_s /= max;
            }
            hsv_v = max;
            r = (int) (128.0f + 0.5f * hsv_s * cos(hsv_h * ctorad)); // +0.5f for round
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (int) (128.0f + 0.5f * hsv_s * sin(hsv_h * ctorad)); // +0.5f for round
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (int) (0.5f + hsv_v); // +0.5f for round
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            rowh[x] = qRgb(r, g, b);
        }
    }

    return hsv_img;
}

QImage imageHSLcylinder(QImage const& image)
{
    if (image.isNull())
    {
        return image;
    }

    unsigned int const w = image.width();
    unsigned int const h = image.height();

    QImage hsl_img(w, h, QImage::Format_RGB32);
    if (hsl_img.isNull())
    {
        return QImage();
    }

    float ctorad = (float)(2.0 * M_PI / 256.0);

    for (unsigned int y = 0; y < h; y++)
    {
        QRgb *rowh = (QRgb*)hsl_img.constScanLine(y);
        for (unsigned int x = 0; x < w; x++)
        {
            QRgb pixel = image.pixel(x, y);
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);
            float hsl_h = 0.0f, hsl_s = 0.0f, hsl_l = 0.0f;
            float hsl_d, hsl_p;
            int max = 0, min = 255;
            max = (r < g) ? g : r;
            max = (max < b) ? b : max;
            min = (r > g) ? g : r;
            min = (min > b) ? b : min;
            hsl_d = max - min;
            hsl_p = max + min;
            if (hsl_d > 0.0f)
            {
                if (max == r)
                {
                    hsl_h = (g - b) * 256.0f / hsl_d;
                }
                else if (max == g)
                {
                    hsl_h = (b - r) * 256.0f / hsl_d + 512;
                }
                else
                {
                    hsl_h = (r - g) * 256.0f / hsl_d + 1024;
                }
                hsl_h /= 6.0f;
                hsl_h = (hsl_h < 0.0f) ? (hsl_h + 256.0f) : hsl_h;
                hsl_s = 256.0f * hsl_d / (256.0f - fabs(256.0f - hsl_p));
            }
            hsl_l = 0.5f * hsl_p;
            r = (int) (128.0f + 0.5f * hsl_s * cos(hsl_h * ctorad));
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (int) (128.0f + 0.5f * hsl_s * sin(hsl_h * ctorad));
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (int) (0.5f + hsl_l); // +0.5f for round
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            rowh[x] = qRgb(r, g, b);
        }
    }

    return hsl_img;
}

QImage imageYCbCr(QImage const& image)
{
    if (image.isNull())
    {
        return image;
    }

    unsigned int const w = image.width();
    unsigned int const h = image.height();

    QImage ycbcr_img(w, h, QImage::Format_RGB32);
    if (ycbcr_img.isNull())
    {
        return QImage();
    }

    for (unsigned int y = 0; y < h; y++)
    {
        QRgb *rowh = (QRgb*)ycbcr_img.constScanLine(y);
        for (unsigned int x = 0; x < w; x++)
        {
            QRgb pixel = image.pixel(x, y);
            int r = qRed(pixel);
            int g = qGreen(pixel);
            int b = qBlue(pixel);

            float cy = 0.299f  * r + 0.587f * g + 0.114f * b;
            float cb = 128.0f - 0.168736f * r - 0.331264f * g + 0.5f * b;
            float cr = 128.0f + 0.5f * r - 0.418688f * g - 0.081312f * b;

            r = (int) (0.5f + cb);
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (int) (0.5f + cr);
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (int) (0.5f + cy);
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            rowh[x] = qRgb(r, g, b);
        }
    }

    return ycbcr_img;
}

float pixelDistance(
    float const h0, float const s0, float const v0,
    float const h1, float const s1, float const v1)
{
    float const delta_h = h0 - h1;
    float const delta_s = s0 - s1;
    float const delta_v = v0 - v1;
    float const delta_h2 = delta_h * delta_h;
    float const delta_s2 = delta_s * delta_s;
    float const delta_v2 = delta_v * delta_v;
    float const dist = delta_h2 + delta_h2 + delta_s2 + delta_s2 + delta_v2;

    return dist;
}

void paletteHSVcylinderGenerate(
    double* mean_h0, double* mean_s0, double* mean_v0,
    int const ncount, float const start_value)
{
    if (ncount > 0)
    {
        float ctorad = (float)(2.0 * M_PI / 256.0);
        float const fk = 256.0f / (float) ncount;

        for (int i = 1; i <= ncount; i++)
        {
            float const hsv_h = ((float) i - 0.5f) * fk;
            mean_h0[i] = 128.0 * (1.0 + cos(hsv_h * ctorad));
            mean_s0[i] = 128.0 * (1.0 + sin(hsv_h * ctorad));
            mean_v0[i] = start_value;
        }
    }
}

void paletteHSVcylinderToHSV(
    double* mean_h, double* mean_s,
    int const start, int const stop)
{
    if ((stop - start) > 0)
    {
        float ctorad = (float)(2.0 * M_PI / 256.0);

        for (int k = start; k < stop; k++)
        {
            float const hsv_hsc = (mean_h[k] - 128.0f) * 2.0f;
            float const hsv_hss = (mean_s[k] - 128.0f) * 2.0f;
            float hsv_h = atan2(hsv_hss, hsv_hsc) / ctorad;
            hsv_h = (hsv_h < 0.0f) ? (hsv_h + 256.0f) : hsv_h;
            float const hsv_s = sqrt(hsv_hsc * hsv_hsc + hsv_hss * hsv_hss);
            mean_h[k] = hsv_h;
            mean_s[k] = hsv_s;
        }
    }
}

void paletteHSVsaturation(
    double* mean_s, float const coef_sat,
    int const start, int const stop)
{
    if ((stop - start) > 0)
    {
        float min_sat = 512.0f;
        float max_sat = 0.0f;
        for (int k = start; k < stop; k++)
        {
            min_sat = (mean_s[k] < min_sat) ? mean_s[k] : min_sat;
            max_sat = (mean_s[k] > max_sat) ? mean_s[k] : max_sat;
        }
        float d_sat = max_sat - min_sat;
        for (int k = start; k < stop; k++)
        {
            float sat_new = (d_sat > 0.0f) ? ((mean_s[k] - min_sat) * 255.0f / d_sat) : 255.0f;
            sat_new = sat_new * coef_sat + mean_s[k] * (1.0f - coef_sat);
            mean_s[k] = sat_new;
        }
    }
}

void paletteYCbCrsaturation(
    double* mean_cb, double* mean_cr, float const coef_sat,
    int const start, int const stop)
{
    if ((stop - start) > 0)
    {
        float max_sat = 0.0f;
        for (int k = start; k < stop; k++)
        {
            float tsat = (mean_cb[k] < 128.0f) ? (128.0f - mean_cb[k]) : (mean_cb[k] - 128.0f);
            max_sat = (tsat > max_sat) ? tsat : max_sat;
            tsat = (mean_cr[k] < 128.0f) ? (128.0f - mean_cr[k]) : (mean_cr[k] - 128.0f);
            max_sat = (tsat > max_sat) ? tsat : max_sat;
        }
        for (int k = start; k < stop; k++)
        {
            float sat_cb = (max_sat > 0.0f) ? ((mean_cb[k] - 128.0f) * 128.0f / max_sat + 128.0f) : mean_cb[k];
            float sat_cr = (max_sat > 0.0f) ? ((mean_cr[k] - 128.0f) * 128.0f / max_sat + 128.0f) : mean_cr[k];
            sat_cb = sat_cb * coef_sat + mean_cb[k] * (1.0f - coef_sat);
            sat_cr = sat_cr * coef_sat + mean_cr[k] * (1.0f - coef_sat);
            mean_cb[k] = sat_cb;
            mean_cr[k] = sat_cr;
        }
    }
}

void paletteHSVnorm(
    double* mean_v, float const coef_norm,
    int const start, int const stop)
{
    if ((stop - start) > 0)
    {
        float min_vol = 512.0f;
        float max_vol = 0.0f;
        for (int k = start; k < stop; k++)
        {
            min_vol = (mean_v[k] < min_vol) ? mean_v[k] : min_vol;
            max_vol = (mean_v[k] > max_vol) ? mean_v[k] : max_vol;
        }
        float d_vol = max_vol - min_vol;
        for (int k = start; k < stop; k++)
        {
            float vol_new = (d_vol > 0.0f) ? ((mean_v[k] - min_vol) * 255.0f / d_vol) : 0.0f;
            vol_new = vol_new * coef_norm + mean_v[k] * (1.0f - coef_norm);
            mean_v[k] = vol_new;
        }
    }
}


void paletteHSVtoRGB(
    double* mean_h, double* mean_s, double* mean_v, int const ncount)
{
    if (ncount > 0)
    {
        for (int k = 0; k < ncount; k++)
        {
            int r, g, b;
            float const hsv_h = mean_h[k];
            float const hsv_s = mean_s[k];
            float const hsv_v = mean_v[k];
            r = g = b = (int) (hsv_v + 0.5f);
            int const i = (int) (hsv_h * 6.0f / 256.0f) % 6;
            int const vm = (int) ((256.0f - hsv_s) * hsv_v + 127)/ 256;
            int const va = (int) ((hsv_v - vm) * (6.0f * hsv_h - i * 256.0f) + 127)/ 256;
            int const vi = vm + va;
            int const vd = hsv_v - va;
            if (hsv_s > 0.0f)
            {
                switch (i)
                {
                default:
                case 0:
                    g = vi;
                    b = vm;
                    break;
                case 1:
                    r = vd;
                    b = vm;
                    break;
                case 2:
                    r = vm;
                    b = vi;
                    break;
                case 3:
                    r = vm;
                    g = vd;
                    break;
                case 4:
                    r = vi;
                    g = vm;
                    break;
                case 5:
                    g = vm;
                    b = vd;
                    break;
                }
            }
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            mean_h[k] = r;
            mean_s[k] = g;
            mean_v[k] = b;
        }
    }
}

void paletteHSLtoRGB(
    double* mean_h, double* mean_s, double* mean_l, int const ncount)
{
    if (ncount > 0)
    {
        float const p1256 = 1.0f / 256.0f;
        float const p6256 = 6.0f / 256.0f;
        float const p2562 = 256.0f / 2.0f;
        float const p2563 = 256.0f / 3.0f;
        float const p2566 = 256.0f / 6.0f;
        float const p25623 = 256.0f * 2.0f / 3.0f;
        for (int k = 0; k < ncount; k++)
        {
            int r, g, b;
            float const hsl_h = mean_h[k];
            float const hsl_s = mean_s[k];
            float const hsl_l = mean_l[k];
            float const hsl_q = (hsl_l < p2562) ? (hsl_l * (256.0f + hsl_s) * p1256) : (((hsl_l + hsl_s) * 256.0f - hsl_l * hsl_s) * p1256);
            float const hsl_p = 2.0 * hsl_l - hsl_q;
            float const hsl_u = hsl_q - hsl_p;
            float tcr = hsl_h + p2563;
            tcr = (tcr < 256.0f) ? tcr : (tcr - 256.0f);
            float tcg = hsl_h;
            float tcb = hsl_h - p2563;
            tcb = (tcb < 0.0f) ? (tcb + 256.0f): tcb;
            tcr = ((tcr < p2566) ? (hsl_p + hsl_u * tcr * p6256) : (tcr < p2562) ? hsl_q : (tcr < p25623) ? (hsl_p + hsl_u * (p25623 - tcr) * p6256) : hsl_p);
            tcg = ((tcg < p2566) ? (hsl_p + hsl_u * tcg * p6256) : (tcg < p2562) ? hsl_q : (tcg < p25623) ? (hsl_p + hsl_u * (p25623 - tcg) * p6256) : hsl_p);
            tcb = ((tcb < p2566) ? (hsl_p + hsl_u * tcb * p6256) : (tcb < p2562) ? hsl_q : (tcb < p25623) ? (hsl_p + hsl_u * (p25623 - tcb) * p6256) : hsl_p);
            r = (int) (tcr + 0.5f);
            g = (int) (tcg + 0.5f);
            b = (int) (tcb + 0.5f);
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            mean_h[k] = r;
            mean_s[k] = g;
            mean_l[k] = b;
        }
    }
}

void paletteYCbCrtoRGB(
    double* mean_cb, double* mean_cr, double* mean_cy, int const ncount)
{
    if (ncount > 0)
    {
        for (int k = 0; k < ncount; k++)
        {
            int r, g, b;
            float const cb = mean_cb[k];
            float const cr = mean_cr[k];
            float const cy = mean_cy[k];
            float fcr = cy + 1.402f * (cr - 128.0f);
            float fcg = cy - 0.344136f * (cb - 128.0f) - 0.714136f * (cr - 128.0f);
            float fcb = cy + 1.772f * (cb - 128.0f);
            r = (int) (fcr + 0.5f);
            g = (int) (fcg + 0.5f);
            b = (int) (fcb + 0.5f);
            r = (r < 0) ? 0 : (r < 255) ? r : 255;
            g = (g < 0) ? 0 : (g < 255) ? g : 255;
            b = (b < 0) ? 0 : (b < 255) ? b : 255;
            mean_cb[k] = r;
            mean_cr[k] = g;
            mean_cy[k] = b;
        }
    }
}

double hsvKMeansInPlace(
    QImage& dst,
    QImage const& image,
    BinaryImage const& mask,
    BinaryImage const& mask_zones,
    int const ncount,
    int const start_value,
    int const color_space,
    float const coef_sat,
    float const coef_norm,
    float const coef_bg,
    bool const find_black,
    bool const find_white)
{
    double mse = 0.0;
    if (dst.isNull() || image.isNull() || mask.isNull())
    {
        return mse;
    }

    if ((ncount > 0) && (ncount <= 254))
    {
        unsigned int const w = dst.width();
        unsigned int const h = dst.height();
        unsigned int const wi = image.width();
        unsigned int const hi = image.height();
        unsigned int const wm = mask.width();
        unsigned int const hm = mask.height();
        unsigned int const wmz = mask_zones.width();
        unsigned int const hmz = mask_zones.height();

        if ((w != wi) || (h != hi) || (w != wm) || (h != hm) || (w != wmz) || (h != hmz))
        {
            return mse;
        }

        int const bgcount = (int) (coef_bg * (float) ncount + 0.25f);
        int const fgcount = ncount - bgcount;
        int const start = (fgcount > 0) ? 1 : 0;
        int const sfull = ncount + 2;
        int const stop = (bgcount > 0) ? (sfull - 1) : sfull;

        uint32_t const* mask_line = mask.data();
        int const mask_stride = mask.wordsPerLine();
        uint32_t const* mask_zones_line = mask_zones.data();
        int const mask_zones_stride = mask_zones.wordsPerLine();

        /* RGB to CS */
        QImage hsv_img;
        switch (color_space)
        {
        case 0:
        {
            // HSV
            hsv_img = imageHSVcylinder(image);
            break;
        }
        case 1:
        {
            // HSL
            hsv_img = imageHSLcylinder(image);
            break;
        }
        case 2:
        {
            // YCbCr
            hsv_img = imageYCbCr(image);
            break;
        }
        default:
        {
            // HSV
            hsv_img = imageHSVcylinder(image);
            break;
        }
        }

        if (hsv_img.isNull())
        {
            return mse;
        }

        /* len clusters*/
        uint32_t mean_len[256] = {0};
        /* defaults clusters*/
        double mean_h0[256] = {0.0};
        double mean_s0[256] = {0.0};
        double mean_v0[256] = {0.0};
        /* work clusters*/
        double mean_h[256] = {0.0};
        double mean_s[256] = {0.0};
        double mean_v[256] = {0.0};
        /* sort clusters*/
        float dist_c[256] = {0.0f}, dist_c_max = 0.0f;
        bool dist_bg[256] = {false};
        /* reinit clusters*/
        unsigned int mean_y[256] = {0};
        unsigned int mean_x[256] = {0};

        uint32_t const msb = uint32_t(1) << 31;

        GrayImage clusters(image);
        if (clusters.isNull())
        {
            return mse;
        }

        unsigned char* clusters_line = clusters.data();
        int const clusters_stride = clusters.stride();

        /* init clusters */
        paletteHSVcylinderGenerate(mean_h0, mean_s0, mean_v0, ncount, start_value);

        /* find and sort clusters */
        for (int i = 1; i <= ncount; i++)
        {
            mask_line = mask.data();
            mask_zones_line = mask_zones.data();
            float dist_min = -1.0f;
            for (unsigned int y = 0; y < h; y++)
            {
                QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
                for (unsigned int x = 0; x < w; x++)
                {
                    if (mask_zones_line[x >> 5] & (msb >> (x & 31)))
                    {
                        float const hsv_h = qRed(rowh[x]);
                        float const hsv_s = qGreen(rowh[x]);
                        float const hsv_v = qBlue(rowh[x]);
                        float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h0[i], mean_s0[i], mean_v0[i]);
                        if ((dist_min < 0.0f) || (dist < dist_min))
                        {
                            mean_h[i] = hsv_h;
                            mean_s[i] = hsv_s;
                            mean_v[i] = hsv_v;
                            dist_c[i] = dist;
                            dist_min = dist;
                            dist_bg[i] = (mask_line[x >> 5] & (msb >> (x & 31))) ? false : true;
                        }
                    }
                }
                mask_line += mask_stride;
                mask_zones_line += mask_zones_stride;
            }
            dist_c_max = (dist_c_max < dist_c[i]) ? dist_c[i] : dist_c_max;
            mean_h0[i] = mean_h[i];
            mean_s0[i] = mean_s[i];
            mean_v0[i] = mean_v[i];
        }
        dist_c_max *= 2.0f;
        for (int i = 1; i <= ncount; i++)
        {
            dist_c[i] = (dist_bg[i]) ? (dist_c_max - dist_c[i]) : dist_c[i];
        }
        for (int i = 1; i <= ncount; i++)
        {
            float dist_min = -1.0f;
            int dist_min_num = 0;
            for (int j = 1; j <= ncount; j++)
            {
                float const dist = dist_c[j];
                if ((dist_min < 0.0f) || (dist < dist_min))
                {
                    dist_min_num = j;
                    dist_min = dist;
                }
            }
            mean_h0[i] = mean_h[dist_min_num];
            mean_s0[i] = mean_s[dist_min_num];
            mean_v0[i] = mean_v[dist_min_num];
            dist_c[dist_min_num] += dist_c_max;
        }
        mean_h0[0] = 128.0;
        mean_s0[0] = 128.0;
        mean_v0[0] = 128.0;
        mean_h0[ncount + 1] = 128.0;
        mean_s0[ncount + 1] = 128.0;
        mean_v0[ncount + 1] = 128.0;
        for (int i = 0; i < (ncount + 2); i++)
        {
            mean_h[i] = mean_h0[i];
            mean_s[i] = mean_s0[i];
            mean_v[i] = mean_v0[i];
        }

        /* reinit clusters (separate fg and bg) */
        for (int i = 1; i <= ncount; i++)
        {
            mask_line = mask.data();
            mask_zones_line = mask_zones.data();
            float dist_min = -1.0f;
            for (unsigned int y = 0; y < h; y++)
            {
                QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
                for (unsigned int x = 0; x < w; x++)
                {
                    if (mask_zones_line[x >> 5] & (msb >> (x & 31)))
                    {
                        // != on bools: the masked bit is not 0 or 1.
                        if ((i > fgcount) != ((mask_line[x >> 5] & (msb >> (x & 31))) != 0))
                        {
                            float const hsv_h = qRed(rowh[x]);
                            float const hsv_s = qGreen(rowh[x]);
                            float const hsv_v = qBlue(rowh[x]);
                            float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h0[i], mean_s0[i], mean_v0[i]);
                            if ((dist_min < 0.0f) || (dist < dist_min))
                            {
                                mean_h[i] = hsv_h;
                                mean_s[i] = hsv_s;
                                mean_v[i] = hsv_v;
                                mean_y[i] = y;
                                mean_x[i] = x;
                                dist_min = dist;
                            }
                        }
                    }
                }
                mask_line += mask_stride;
                mask_zones_line += mask_zones_stride;
            }
            mean_h0[i] = mean_h[i];
            mean_s0[i] = mean_s[i];
            mean_v0[i] = mean_v[i];
        }

        /* init clusters map */
        mask_line = mask.data();
        for (unsigned int y = 0; y < h; y++)
        {
            QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
            for (unsigned int x = 0; x < w; x++)
            {
                float const hsv_h = qRed(rowh[x]);
                float const hsv_s = qGreen(rowh[x]);
                float const hsv_v = qBlue(rowh[x]);
                float dist_min = -1.0f;
                int indx_min = 0;
                if (mask_line[x >> 5] & (msb >> (x & 31)))
                {
                    if (fgcount > 0)
                    {
                        for (int k = 1; k <= fgcount; k++)
                        {
                            float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h[k], mean_s[k], mean_v[k]);
                            if ((dist_min < 0.0f) || (dist < dist_min))
                            {
                                indx_min = k;
                                dist_min = dist;
                            }
                        }
                    }
                    else
                    {
                        indx_min = 0;
                    }
                }
                else
                {
                    if (bgcount > 0)
                    {
                        for (int k = (fgcount + 1); k <= ncount; k++)
                        {
                            float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h[k], mean_s[k], mean_v[k]);
                            if ((dist_min < 0.0f) || (dist < dist_min))
                            {
                                indx_min = k;
                                dist_min = dist;
                            }
                        }
                    }
                    else
                    {
                        indx_min = ncount + 1;
                    }
                }
                clusters_line[x] = indx_min;
            }
            mask_line += mask_stride;
            clusters_line += clusters_stride;
        }

        /* iteration clusters map */
        for (unsigned int itr = 0; itr < 50; itr++)
        {
            for (int i = 0; i < sfull; i++)
            {
                mean_h[i] = 0.0;
                mean_s[i] = 0.0;
                mean_v[i] = 0.0;
                mean_len[i] = 0;
            }

            mask_zones_line = mask_zones.data();
            clusters_line = clusters.data();
            for (unsigned int y = 0; y < h; y++)
            {
                QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
                for (unsigned int x = 0; x < w; x++)
                {
                    if (mask_zones_line[x >> 5] & (msb >> (x & 31)))
                    {
                        int const cluster = clusters_line[x];
                        float const hsv_h = qRed(rowh[x]);
                        float const hsv_s = qGreen(rowh[x]);
                        float const hsv_v = qBlue(rowh[x]);
                        mean_h[cluster] += hsv_h;
                        mean_s[cluster] += hsv_s;
                        mean_v[cluster] += hsv_v;
                        mean_len[cluster]++;
                    }
                }
                mask_zones_line += mask_zones_stride;
                clusters_line += clusters_stride;
            }
            clusters_line = clusters.data();
            uint32_t changes = 0;
            for (int i = 0; i < sfull; i++)
            {
                if (mean_len[i] > 0)
                {
                    float const mean_lr = 1.0f / (float) mean_len[i];
                    mean_h[i] *= mean_lr;
                    mean_s[i] *= mean_lr;
                    mean_v[i] *= mean_lr;
                }
                else
                {
                    mean_h[i] = mean_h0[i];
                    mean_s[i] = mean_s0[i];
                    mean_v[i] = mean_v0[i];

                    unsigned int y0 = mean_y[i];
                    unsigned int x0 = mean_x[i];
                    uint32_t indx = y0 * clusters_stride + x0;
                    int const cluster = clusters_line[indx];
                    mean_len[cluster]--;
                    clusters_line[indx] = i;
                    mean_len[i] = 1;
                    changes++;
                }
            }

            mask_line = mask.data();
            clusters_line = clusters.data();
            for (unsigned int y = 0; y < h; y++)
            {
                QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
                for (unsigned int x = 0; x < w; x++)
                {
                    float const hsv_h = qRed(rowh[x]);
                    float const hsv_s = qGreen(rowh[x]);
                    float const hsv_v = qBlue(rowh[x]);
                    float dist_min = -1.0f;
                    int indx_min = 0;
                    if (mask_line[x >> 5] & (msb >> (x & 31)))
                    {
                        if (fgcount > 0)
                        {
                            for (int k = 1; k <= fgcount; k++)
                            {
                                float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h[k], mean_s[k], mean_v[k]);
                                if ((dist_min < 0.0f) || (dist < dist_min))
                                {
                                    indx_min = k;
                                    dist_min = dist;
                                }
                            }
                        }
                        else
                        {
                            indx_min = 0;
                        }
                    }
                    else
                    {
                        if (bgcount > 0)
                        {
                            for (int k = (fgcount + 1); k <= ncount; k++)
                            {
                                float const dist = pixelDistance(hsv_h, hsv_s, hsv_v, mean_h[k], mean_s[k], mean_v[k]);
                                if ((dist_min < 0.0f) || (dist < dist_min))
                                {
                                    indx_min = k;
                                    dist_min = dist;
                                }
                            }
                        }
                        else
                        {
                            indx_min = ncount + 1;
                        }
                    }
                    if (indx_min != clusters_line[x])
                    {
                        clusters_line[x] = indx_min;
                        changes++;
                    }

                }
                mask_line += mask_stride;
                clusters_line += clusters_stride;
            }

            if (changes == 0)
            {
                break;
            }
        }

        /* Norm abd Sat */
        if (color_space < 2)
        {
            paletteHSVcylinderToHSV(mean_h, mean_s, start, stop);
            paletteHSVsaturation(mean_s, coef_sat, start, stop);
        }
        else
        {
            paletteYCbCrsaturation(mean_h, mean_s, coef_sat, start, stop);
        }
        paletteHSVnorm(mean_v, coef_norm, start, stop);

        /* find dominantes black and white */
        int k_black = 0;
        int k_white = sfull;
        if (find_black)
        {
            float sv_min = 256.0f * 512.0f;
            for (int k = start; k <= fgcount; k++)
            {
                float const value = mean_v[k] + 1.0f;
                float sat = 1.0f;
                if (color_space == 2)
                {
                    float const cb = mean_h[k] - 128.0f;
                    float const cr = mean_s[k] - 128.0f;
                    sat = sqrtf(cb * cb + cr * cr) + 1.0f;
                }
                else
                {
                    sat = mean_s[k] + 1.0f;
                }
                float const sv_k = value * sat;
                if (sv_min > sv_k)
                {
                    sv_min = sv_k;
                    k_black = k;
                }
            }
        }
        if (find_white)
        {
            float sv_max = 0.0f;
            for (int k = (fgcount + 1); k < stop; k++)
            {
                float const value = mean_v[k] + 1.0f;
                float sat = 1.0f;
                if (color_space == 2)
                {
                    float const cb = mean_h[k] - 128.0f;
                    float const cr = mean_s[k] - 128.0f;
                    sat = sqrtf(cb * cb + cr * cr) + 1.0f;
                }
                else
                {
                    sat = mean_s[k] + 1.0f;
                }
                float const sv_k = value / sat;
                if (sv_max < sv_k)
                {
                    sv_max = sv_k;
                    k_white = k;
                }
            }
        }

        /* CS to RGB */
        switch (color_space)
        {
        case 0:
        {
            // HSV
            paletteHSVtoRGB(mean_h, mean_s, mean_v, sfull);
            break;
        }
        case 1:
        {
            // HSL
            paletteHSLtoRGB(mean_h, mean_s, mean_v, sfull);
            break;
        }
        case 2:
        {
            // YCbCr
            paletteYCbCrtoRGB(mean_h, mean_s, mean_v, sfull);
            break;
        }
        default:
        {
            // HSV
            paletteHSVtoRGB(mean_h, mean_s, mean_v, sfull);
            break;
        }
        }

        /* replace dominantes black and white */
        if (find_black)
        {
            mean_h[k_black] = 0.0;
            mean_s[k_black] = 0.0;
            mean_v[k_black] = 0.0;
        }
        if (find_white)
        {
            mean_h[k_white] = 255.0;
            mean_s[k_white] = 255.0;
            mean_v[k_white] = 255.0;
        }

        mean_h[0] = 0.0;
        mean_s[0] = 0.0;
        mean_v[0] = 0.0;
        mean_h[ncount + 1] = 255.0;
        mean_s[ncount + 1] = 255.0;
        mean_v[ncount + 1] = 255.0;

        /* Replace pixels and metrics */
        uint32_t cntkm = 0;
        mask_zones_line = mask_zones.data();
        clusters_line = clusters.data();
        for (unsigned int y = 0; y < h; y++)
        {
            QRgb *rowh = (QRgb*)hsv_img.constScanLine(y);
            double msel = 0.0;
            for (unsigned int x = 0; x < w; x++)
            {
                int r, g, b;
                if (mask_zones_line[x >> 5] & (msb >> (x & 31)))
                {
                    int r0, g0, b0, dr, dg, db;
                    QRgb const origin = image.pixel(x, y);
                    r0 = qRed(origin);
                    g0 = qGreen(origin);
                    b0 = qBlue(origin);
                    int const cluster = clusters_line[x];
                    r = mean_h[cluster];
                    g = mean_s[cluster];
                    b = mean_v[cluster];
                    dr = r0 - r;
                    dg = g0 - g;
                    db = b0 - b;
                    double dt = dr * dr + dg * dg + db * db;
                    msel += dt;
                    cntkm++;
                }
                else
                {
                    QRgb const pixel = dst.pixel(x, y);
                    r = qRed(pixel);
                    g = qGreen(pixel);
                    b = qBlue(pixel);
                }
                rowh[x] = qRgb(r, g, b);
            }
            mse += msel;
            mask_zones_line += mask_zones_stride;
            clusters_line += clusters_stride;
        }
        mse = (cntkm > 0) ? (sqrt(mse / cntkm / 3.0) / 255.0) : 0.0;

        dst = hsv_img;
    }

    return mse;
}

} // namespace stexp
} // namespace imageproc
