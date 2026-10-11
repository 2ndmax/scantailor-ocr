// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Methods.h"

#include "Binarize.h"
#include "BinaryImage.h"
#include "ExpBinarize.h"
#include "ExpGrayImage.h"

namespace binarization_compare {
namespace {
using imageproc::BinaryImage;
using imageproc::GrayImage;
namespace stexp = imageproc::stexp;

QImage bw(const BinaryImage& image) {
  return image.toQImage();
}

std::vector<Method> createMethods() {
  // Our defaults as in BlackWhiteOptions, the others as set by the options panel of ScanTailor
  // Experimental when the method is chosen (stages/output/OptionsWidget.cpp).
  const QSize window(200, 200);
  return {
      {"st_otsu", "Otsu", "ours", "global",
       [](const GrayImage& g) { return bw(imageproc::binarizeOtsu(g.toQImage())); }},
      {"st_sauvola", "Sauvola", "ours", "window 200, k 0.34",
       [=](const GrayImage& g) { return bw(imageproc::binarizeSauvola(g.toQImage(), window, 0.34)); }},
      {"st_wolf", "Wolf", "ours", "window 200, k 0.3, bounds 1-254",
       [=](const GrayImage& g) { return bw(imageproc::binarizeWolf(g.toQImage(), window, 1, 254, 0.3)); }},
      {"st_edgediv", "EdgeDiv", "ours", "window 200, k 0.34 / 0.34",
       [=](const GrayImage& g) { return bw(imageproc::binarizeEdgeDiv(g.toQImage(), window, 0.34, 0.34)); }},
      {"exp_niblack", "Niblack", "experimental", "radius 100, k 0.2",
       [](const GrayImage& g) { return bw(stexp::binarizeNiblack(g, 100, 0.2f)); }},
      {"exp_gatos", "Gatos", "experimental", "radius 100, noise 3, k 0.2",
       [](const GrayImage& g) { return bw(stexp::binarizeGatos(g, 100, 3.0f, 0.2f)); }},
      {"exp_nick", "Nick", "experimental", "radius 100, k 0.1",
       [](const GrayImage& g) { return bw(stexp::binarizeNick(g, 100, 0.1f)); }},
      {"exp_singh", "Singh", "experimental", "radius 100, k 0.3",
       [](const GrayImage& g) { return bw(stexp::binarizeSingh(g, 100, 0.3f)); }},
      {"exp_wan", "WAN", "experimental", "radius 100, k 0.3",
       [](const GrayImage& g) { return bw(stexp::binarizeWAN(g, 100, 0.3f)); }},
      {"exp_robust", "Robust", "experimental", "radius 20, k 0.2",
       [](const GrayImage& g) { return bw(stexp::binarizeRobust(g, 20, 0.2f)); }},
      {"exp_mscale", "MultiScale", "experimental", "radius 20, k 0.2",
       [](const GrayImage& g) { return bw(stexp::binarizeMScale(g, 20, 0.2f)); }},
      {"exp_bmtiled", "BMTiled", "experimental", "radius 50, k 0.75",
       [](const GrayImage& g) { return bw(stexp::binarizeBMTiled(g, 50, 0.75f)); }},
      {"exp_grain", "Grain", "experimental", "radius 15, k 0.75",
       [](const GrayImage& g) { return bw(stexp::binarizeGrain(g, 15, 0.75f)); }},
      {"exp_grainem", "GrainEM", "experimental", "radius 5, k 0.75",
       [](const GrayImage& g) { return bw(stexp::binarizeGrainEM(g, 5, 0.75f)); }},
      {"exp_lcam", "LCaM", "experimental", "radius 5, k 0.75",
       [](const GrayImage& g) { return bw(stexp::binarizeLCaM(g, 5, 0.75f)); }},
      {"exp_laab", "LAAB", "experimental", "radius 25, k 0.1",
       [](const GrayImage& g) { return bw(stexp::binarizeLAAB(g, 25, 0.1f)); }},
      {"exp_obbpm", "OBBPM", "experimental", "radius 100, k 0.1",
       [](const GrayImage& g) { return bw(stexp::binarizeOBBPM(g, 100, 0.1f)); }},
      {"exp_mean", "Mean", "experimental", "global", [](const GrayImage& g) { return bw(stexp::binarizeMean(g, 0)); }},
      {"exp_edgeadapt", "EdgeAdapt", "experimental", "radius 5, k 0.75",
       [](const GrayImage& g) { return bw(stexp::binarizeEdgeDiv(g, 5, -1.0f, 0.75f)); }},
      {"flt_em", "EM denoiser", "filter", "radius 9, strength 0.5",
       [](const GrayImage& g) { return stexp::grayEMDenoiser(g, 9, 0.5f).toQImage(); }},
      {"flt_autolevel", "Auto level", "filter", "radius 10, strength 1",
       [](const GrayImage& g) { return stexp::grayAutoLevel(g, 10, 1.0f).toQImage(); }},
  };
}
}  // namespace

const std::vector<Method>& methods() {
  static const std::vector<Method> list = createMethods();
  return list;
}
}  // namespace binarization_compare
