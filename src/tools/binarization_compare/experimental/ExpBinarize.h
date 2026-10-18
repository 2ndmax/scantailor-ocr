/*
    Scan Tailor - Interactive post-processing tool for scanned pages.
    Copyright (C) 2007-2015  Joseph Artsimovich <joseph.artsimovich@gmail.com>

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

#pragma once

#include <QSize>
#include "BinaryImage.h"
#include "GrayImage.h"

namespace imageproc {
namespace stexp {

/**
 * \brief Image binarization using Otsu's global thresholding method.
 *
 * \see Help -> About -> References -> [7]
 */
BinaryImage binarizeOtsu(QImage const& src, int delta = 0);

/**
 * \brief Image binarization using Mokji's global thresholding method.
 *
 * \param src The source image. May be in any format.
 * \param max_edge_width The maximum gradient length to consider.
 * \param min_edge_magnitude The minimum color difference in a gradient.
 * \return A black and white image.
 *
 * \see Help -> About -> References -> [8]
 */
BinaryImage binarizeMokji(QImage const& src, unsigned max_edge_width = 3, unsigned min_edge_magnitude = 20);

/**
 * \brief Image binarization using BiModal (analog Otsu's) global thresholding method.
 *
 * \see Help -> About -> References -> [7]
 */
BinaryImage binarizeUse(GrayImage const& src, unsigned int threshold = 128);
BinaryImage binarizeFromMap(GrayImage const& src,
                            GrayImage const& threshold,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

void binarizeNegate(BinaryImage& src);

unsigned int binarizeBiModalValue(GrayImage const& src,
                                  int delta = 0,
                                  unsigned char lower_bound = 0,
                                  unsigned char upper_bound = 255);
BinaryImage binarizeBiModal(GrayImage const& src,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using DeltaMean global thresholding method.
 *
 * \see Help -> About -> References -> [7]
 */
BinaryImage binarizeMean(GrayImage const& src,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

BinaryImage binarizeDots(GrayImage const& src,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

BinaryImage binarizeImageToDots(GrayImage const& src, BinaryImage const& mask, BinaryImage const& mask_bw);

/**
 * \brief Image binarization using tiled BiModal thresholding method.
 */
BinaryImage binarizeBMTiled(GrayImage const& src,
                            int radius = 50,
                            float k = 0.75f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Niblack's local thresholding method.
 *
 * \see Help -> About -> References -> [9]
 */
BinaryImage binarizeNiblack(GrayImage const& src,
                            int radius = 100,
                            float k = 0.20f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Gatos' local thresholding method.
 *
 * This implementation doesn't include the post-processing steps from
 * the above paper.
 *
 * \see Help -> About -> References -> [10]
 */
GrayImage binarizeGatosBG(GrayImage const& src, BinaryImage const& niblack, int radius = 100);
BinaryImage binarizeGatos(GrayImage const& src,
                          int radius = 100,
                          float noise_sigma = 3.0f,
                          float k = 0.20f,
                          int delta = 0,
                          unsigned char lower_bound = 0,
                          unsigned char upper_bound = 255,
                          float q = 0.6f,
                          float p = 0.2f);

/**
 * \brief Image binarization using Sauvola's local thresholding method.
 *
 * \see Help -> About -> References -> [11]
 */
BinaryImage binarizeSauvola(GrayImage const& src,
                            int radius = 100,
                            float k = 0.30f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Wolf's local thresholding method.
 *
 * \see Help -> About -> References -> [12]
 *
 * \param src The image to binarize.
 * \param radius The dimensions of a pixel neighborhood to consider.
 * \param lower_bound The minimum possible gray level that can be made white.
 * \param upper_bound The maximum possible gray level that can be made black.
 */
BinaryImage binarizeWolf(GrayImage const& src,
                         int radius = 100,
                         float k = 0.30f,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Dynamic Window based thresholding method.
 *
 * Bataineh, B., Abdullah, S. N. H. S., & Omar, K. (2011).
 * An adaptive local binarization method for document images based
 * on a novel thresholding method and  dynamic windows.
 * Pattern Recognition Letters, 32(14), 1805–1813.
 */
BinaryImage binarizeWindow(GrayImage const& src,
                           int radius = 50,
                           float k = 1.0f,
                           int delta = 0,
                           unsigned char lower_bound = 0,
                           unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Bradley's adaptive thresholding method.
 *
 * Derek Bradley, Gerhard Roth. 2005. "Adaptive Thresholding Using the Integral Image".
 * http://www.scs.carleton.ca/~roth/iit-publications-iti/docs/gerh-50002.pdf
 */
BinaryImage binarizeBradley(GrayImage const& src,
                            int radius = 100,
                            float k = 0.20f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using N.I.C.K.'s local thresholding method.
 *
 * Khurram Khurshid, Imran Siddiqi, Claudie Faure, Nicole Vincent.
 * "Comparison of Niblack inspired Binarization methods for ancient documents", 2009.
 */
BinaryImage binarizeNick(GrayImage const& src,
                         int radius = 100,
                         float k = 0.10f,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Grad local/global thresholding method.
 *
 * Grad, zvezdochiot 2024. "Adaptive/global document image binarization".
 */
float binarizeGradValue(GrayImage const& gray, GrayImage const& gmean);
BinaryImage binarizeGrad(GrayImage const& src,
                         int radius = 10,
                         float coef = 0.75f,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Singh's adaptive thresholding method.
 *
 * Singh, O. I., Sinam, T., James, O., & Singh, T. R. (2012).
 * Local contrast and mean based thresholding technique in image binarization.
 * International Journal of Computer Applications, 51, 5-10.
 * https://research.ijcaonline.org/volume51/number6/pxc3881362.pdf
 */
BinaryImage binarizeSingh(GrayImage const& src,
                          int radius = 100,
                          float k = 0.30f,
                          int delta = 0,
                          unsigned char lower_bound = 0,
                          unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Local Adaptive Automatic Binarisation (LAAB).
 *
 * Romen Taiyenjam, Sudipta Roy, Khumanthem Manglem Singh
 * February 2012
 * International Journal of Computer Applications 40(6):27-30
 * DOI:10.5120/4961-7218
 * https://www.researchgate.net/publication/258650941_Local_Adaptive_Automatic_Binarisation_LAAB
 */
BinaryImage binarizeLAAB(GrayImage const& src,
                         int radius = 25,
                         float k = 0.1f,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Local Contrast and Mean based Thresholding Technique in Image Binarization.
 *
 * O. Imocha Singh, Tejmani Sinam, O. James, T.Romen Singh
 * International Journal of Computer Applications (0975 – 8887)
 * Volume 51– No.6, August 2012
 * https://research.ijcaonline.org/volume51/number6/pxc3881362.pdf
 */
BinaryImage binarizeLCaM(GrayImage const& src,
                         int radius = 5,
                         float k = 0.75f,
                         int delta = 0,
                         unsigned char lower_bound = 0,
                         unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Fox adaptive thresholding method.
 *
 * Modification of the Wolf threshold using the Singh optimization of the Sauvola threshold.
 */
BinaryImage binarizeFox(GrayImage const& src,
                        int radius = 100,
                        float k = 0.30f,
                        int delta = 0,
                        unsigned char lower_bound = 0,
                        unsigned char upper_bound = 255);
/**
 * \brief Image binarization using WAN's local thresholding method.
 *
 * Wan Azani Mustafa and Mohamed Mydin M. Abdul Kader
 * "Binarization of Document Image Using Optimum Threshold Modification", 2018.
 * https://www.researchgate.net/publication/326026836_Binarization_of_Document_Image_Using_Optimum_Threshold_Modification
 */
BinaryImage binarizeWAN(GrayImage const& src,
                        int radius = 100,
                        float k = 0.30f,
                        int delta = 0,
                        unsigned char lower_bound = 0,
                        unsigned char upper_bound = 255);

/**
 * \brief Image binarization using OBBPM's local thresholding method.
 *
 * O. Imocha Singh and T. Romen Singh / Journal of Computer Science 2017, 13 (11): 667.673
 * DOI: 10.3844/jcssp.2017.667.673
 *
 * This Overlay mod BBPM.
 */
BinaryImage binarizeOBBPM(GrayImage const& src,
                          int radius = 100,
                          float k = 0.10f,
                          int delta = 0,
                          unsigned char lower_bound = 0,
                          unsigned char upper_bound = 255);

/**
 * \brief Image binarization using EdgeDiv (EdgePlus & BlurDiv) local/global thresholding method.
 *
 * EdgeDiv, zvezdochiot 2023. "Adaptive/global document image binarization".
 */
BinaryImage binarizeEdgeDiv(GrayImage const& src,
                            int radius = 10,
                            float kep = 0.50f,
                            float kdb = 0.50f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using Robust's local thresholding method.
 *
 * Tom Liao
 * "Robust document binarization with OFF center-surround cells", 2017-08-09.
 * https://www.researchgate.net/publication/226333284_Robust_document_binarization_with_OFF_center-surround_cells
 */
BinaryImage binarizeRobust(GrayImage const& src,
                           int radius = 10,
                           float k = 0.20f,
                           int delta = 0,
                           unsigned char lower_bound = 0,
                           unsigned char upper_bound = 255);

/**
 * \brief Image prefilter Grain for local/global thresholding method.
 *
 * Grain, zvezdochiot 2025. "Adaptive/global document image binarization".
 */
BinaryImage binarizeGrain(GrayImage const& src,
                          int radius = 15,
                          float k = 0.50f,
                          int delta = 0,
                          unsigned char lower_bound = 0,
                          unsigned char upper_bound = 255);
BinaryImage binarizeGrainEM(GrayImage const& src,
                            int radius = 5,
                            float k = 0.75f,
                            int delta = 0,
                            unsigned char lower_bound = 0,
                            unsigned char upper_bound = 255);

/**
 * \brief Image binarization using MultiScale thresholding method.
 *
 * MultiScale thresholding method.
 */
BinaryImage binarizeMScale(GrayImage const& src,
                           int radius = 10,
                           float coef = 0.50f,
                           int delta = 0,
                           unsigned char lower_bound = 0,
                           unsigned char upper_bound = 255);

BinaryImage binarizeEngraving(GrayImage const& src,
                              int radius = 20,
                              float coef = 0.20f,
                              int delta = 0,
                              unsigned char lower_bound = 0,
                              unsigned char upper_bound = 255);

}  // namespace stexp
}  // namespace imageproc
