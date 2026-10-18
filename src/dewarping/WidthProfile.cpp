// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "WidthProfile.h"

#include <BinaryThreshold.h>
#include <GrayImage.h>

#include <algorithm>
#include <cmath>

namespace dewarping {
namespace {
// Window width in inches (about 7 mm); narrow enough for the compressed strip near the spine.
constexpr double kWindowInches = 0.27;
// Deviations below this are left alone.
constexpr double kMinDeviation = 0.03;
// The correction never stretches or compresses by more than this factor.
constexpr double kMaxScale = 1.4;
// The measured windows have to span at least this part of the model width.
constexpr double kMinCoverage = 0.6;

// Mean without the shortest and the longest tenth.
double trimmedMean(std::vector<int>& values) {
  std::sort(values.begin(), values.end());
  const size_t cut = values.size() / 10;
  double sum = 0;
  for (size_t i = cut; i < values.size() - cut; ++i) {
    sum += values[i];
  }
  return sum / static_cast<double>(values.size() - 2 * cut);
}

double median(std::vector<double> values) {
  std::nth_element(values.begin(), values.begin() + values.size() / 2, values.end());
  return values[values.size() / 2];
}
}  // namespace

std::vector<WidthProfile::Sample> WidthProfile::measure(const imageproc::GrayImage& image,
                                                        const QRect& area,
                                                        const int dpi) {
  const QRect rect = area.intersected(QRect(0, 0, image.width(), image.height()));
  if (rect.isEmpty() || (dpi <= 0)) {
    return {};
  }
  const int threshold = imageproc::BinaryThreshold::otsuThreshold(image.toQImage().copy(rect));
  const uint8_t* const data = image.data();
  const int stride = image.stride();
  const auto dark = [&](int x, int y) { return data[y * stride + x] < threshold; };

  // Runs longer than this are not strokes of body text.
  const int maxRun = std::max(4, static_cast<int>(std::lround(dpi / 24.0)));
  const int minRun = 2;

  // Horizontal runs (stem widths), by their centre, and vertical runs (stroke thicknesses).
  // Runs touching the edge of the area are cut off and left out.
  std::vector<std::pair<int, int>> horRuns;
  for (int y = rect.top(); y <= rect.bottom(); ++y) {
    int start = -1;
    for (int x = rect.left(); x <= rect.right() + 1; ++x) {
      const bool dk = (x <= rect.right()) && dark(x, y);
      if (dk && (start < 0)) {
        start = x;
      } else if (!dk && (start >= 0)) {
        const int len = x - start;
        if ((start > rect.left()) && (x <= rect.right()) && (len >= minRun) && (len <= maxRun)) {
          horRuns.emplace_back((start + x) / 2, len);
        }
        start = -1;
      }
    }
  }
  std::vector<int> verRuns;
  for (int x = rect.left(); x <= rect.right(); ++x) {
    int start = -1;
    for (int y = rect.top(); y <= rect.bottom() + 1; ++y) {
      const bool dk = (y <= rect.bottom()) && dark(x, y);
      if (dk && (start < 0)) {
        start = y;
      } else if (!dk && (start >= 0)) {
        const int len = y - start;
        if ((start > rect.top()) && (y <= rect.bottom()) && (len >= minRun) && (len <= maxRun)) {
          verRuns.push_back(len);
        }
        start = -1;
      }
    }
  }
  const auto minRuns = static_cast<size_t>(std::max(50, rect.height() / 10));
  if (verRuns.size() < minRuns) {
    return {};
  }
  const double thickness = trimmedMean(verRuns);

  std::sort(horRuns.begin(), horRuns.end());
  const int window = std::max(8, static_cast<int>(std::lround(dpi * kWindowInches)));
  const int step = window / 2;
  std::vector<Sample> candidates;
  std::vector<double> counts;
  std::vector<int> lengths;
  auto first = horRuns.begin();
  for (int x0 = rect.left(); x0 + window <= rect.right() + 1; x0 += step) {
    while ((first != horRuns.end()) && (first->first < x0)) {
      ++first;
    }
    lengths.clear();
    for (auto it = first; (it != horRuns.end()) && (it->first < x0 + window); ++it) {
      lengths.push_back(it->second);
    }
    if (lengths.size() >= minRuns) {
      candidates.push_back({x0 + window / 2.0, trimmedMean(lengths) / thickness});
      counts.push_back(static_cast<double>(lengths.size()));
    }
  }
  if (candidates.empty()) {
    return {};
  }
  // Windows with clearly less text than usual, such as at the ragged end of lines,
  // come out too wide; they are left out.
  const double minCount = 0.5 * median(counts);
  std::vector<Sample> samples;
  for (size_t i = 0; i < candidates.size(); ++i) {
    if (counts[i] >= minCount) {
      samples.push_back(candidates[i]);
    }
  }

  std::vector<double> scales;
  for (const Sample& sample : samples) {
    scales.push_back(sample.scale);
  }
  const double med = median(scales);
  for (Sample& sample : samples) {
    sample.scale /= med;
  }
  return samples;
}

WidthCorrection WidthProfile::toCorrection(const std::vector<Sample>& samples,
                                           const double modelLeft,
                                           const double modelWidth) {
  if ((samples.size() < 5) || (modelWidth <= 0) || (samples.back().x - samples.front().x < kMinCoverage * modelWidth)) {
    return WidthCorrection();
  }

  // Smooth: median of three, then a [1 2 1] average.
  const size_t n = samples.size();
  std::vector<double> med(n);
  for (size_t i = 0; i < n; ++i) {
    if ((i == 0) || (i == n - 1)) {
      med[i] = samples[i].scale;
    } else {
      double a = samples[i - 1].scale, b = samples[i].scale, c = samples[i + 1].scale;
      med[i] = std::max(std::min(a, b), std::min(std::max(a, b), c));
    }
  }
  std::vector<double> scale(n);
  for (size_t i = 0; i < n; ++i) {
    const double prev = med[(i == 0) ? i : i - 1];
    const double next = med[(i == n - 1) ? i : i + 1];
    scale[i] = std::clamp((prev + 2 * med[i] + next) / 4, 1.0 / kMaxScale, kMaxScale);
  }

  double maxDeviation = 0;
  for (const double s : scale) {
    maxDeviation = std::max(maxDeviation, std::abs(s - 1.0));
  }
  if (maxDeviation < kMinDeviation) {
    return WidthCorrection();
  }

  // Where the letters are too narrow (scale < 1) the page has to be stretched: the corrected
  // position grows with the density 1 / scale. Beyond the first and last window the nearest
  // measurement is used.
  std::vector<double> u(n);
  for (size_t i = 0; i < n; ++i) {
    u[i] = (samples[i].x - modelLeft) / modelWidth;
  }
  const auto density = [&](double pos) {
    if (pos <= u.front()) {
      return 1.0 / scale.front();
    }
    if (pos >= u.back()) {
      return 1.0 / scale.back();
    }
    const size_t i = static_cast<size_t>(std::upper_bound(u.begin(), u.end(), pos) - u.begin()) - 1;
    const double frac = (pos - u[i]) / (u[i + 1] - u[i]);
    return 1.0 / (scale[i] + (scale[i + 1] - scale[i]) * frac);
  };

  constexpr int kSteps = 1024;
  constexpr int kSamples = 65;
  std::vector<double> integral(kSteps + 1, 0.0);
  for (int i = 1; i <= kSteps; ++i) {
    integral[i] = integral[i - 1] + density((i - 0.5) / kSteps);
  }
  std::vector<double> result(kSamples);
  for (int k = 0; k < kSamples; ++k) {
    result[k] = integral[k * kSteps / (kSamples - 1)] / integral[kSteps];
  }
  result.front() = 0.0;
  result.back() = 1.0;
  return WidthCorrection(result);
}
}  // namespace dewarping
