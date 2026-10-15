// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RenderParams.h"

#include "ColorParams.h"

namespace output {
RenderParams::RenderParams(const ColorParams& colorParams, const SplittingOptions& splittingOptions) : m_mask(0) {
  const BlackWhiteOptions& blackWhiteOptions = colorParams.blackWhiteOptions();
  const ColorCommonOptions& colorCommonOptions = colorParams.colorCommonOptions();
  const ColorMode colorMode = colorParams.colorMode();
  // In mixed mode the pictures are only equalized together with the text. The color setting is
  // kept as it is, for the color mode and for turning the black and white one on again.
  const bool normalizeIlluminationColor = colorCommonOptions.normalizeIllumination()
                                          && ((colorMode != MIXED) || blackWhiteOptions.normalizeIllumination());

  if ((colorMode == BLACK_AND_WHITE) || (colorMode == MIXED)) {
    m_mask |= NEED_BINARIZATION;
    if (colorMode == MIXED) {
      m_mask |= MIXED_OUTPUT;
    }
    if (mixedOutput() && splittingOptions.isSplitOutput()) {
      m_mask |= SPLIT_OUTPUT;
      if (splittingOptions.getSplittingMode() == COLOR_FOREGROUND) {
        m_mask ^= NEED_BINARIZATION;
      }
      if (needBinarization() && splittingOptions.isOriginalBackgroundEnabled()) {
        m_mask |= ORIGINAL_BACKGROUND;
      }
    }
  }
  if (needBinarization()) {
    if (blackWhiteOptions.isSavitzkyGolaySmoothingEnabled()) {
      m_mask |= SAVITZKY_GOLAY_SMOOTHING;
    }
    if (blackWhiteOptions.isMorphologicalSmoothingEnabled()) {
      m_mask |= MORPHOLOGICAL_SMOOTHING;
    }
    if (blackWhiteOptions.normalizeIllumination()) {
      m_mask |= NORMALIZE_ILLUMINATION;
    }
    if (blackWhiteOptions.getColorSegmenterOptions().isEnabled()) {
      m_mask |= COLOR_SEGMENTATION;
      // The text colors have their own posterizing, apart from that of whole color pages.
      if (blackWhiteOptions.getColorSegmenterOptions().isPosterizeEnabled()
          && !blackWhiteOptions.getColorSegmenterOptions().isKMeansEnabled()) {
        m_mask |= POSTERIZE;
      }
    }
  } else {
    if (normalizeIlluminationColor) {
      m_mask |= NORMALIZE_ILLUMINATION;
    }
    if (colorCommonOptions.getPosterizationOptions().isEnabled()) {
      m_mask |= POSTERIZE;
    }
  }
  if (colorCommonOptions.fillMargins()) {
    m_mask |= FILL_MARGINS;
  }
  if (colorCommonOptions.fillOutsidePageBox()) {
    m_mask |= FILL_OUTSIDE_PAGE_BOX;
  } else if (colorCommonOptions.fillOffcut()) {
    m_mask |= FILL_OFFCUT;
  }
  if (normalizeIlluminationColor) {
    m_mask |= NORMALIZE_ILLUMINATION_COLOR;
  }
}
}  // namespace output
