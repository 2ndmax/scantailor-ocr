// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ColorModeDetector.h"

#include <BinaryImage.h>
#include <ConnectivityMap.h>
#include <GrayImage.h>
#include <Scale.h>

#include <QImage>
#include <algorithm>
#include <vector>

#include "PictureDetection.h"
#include "TaskStatus.h"

using namespace imageproc;

namespace {
// Measured on a grayscale-scanned book with photos and on a color atlas (2026-10-11).
const double MIN_COLOR_IN_PICTURES = 0.005;  // Colored share of the page inside picture areas.
const double MIN_PICTURE = 0.25;             // Largest picture area for the second opinion.
const double MAX_PICTURE = 0.95;             // Above this, the "picture" is the empty page itself.
const double MIN_DARK = 0.01;                // Pages with less dark content count as empty.
const int TEXT_GRAY = 110;                   // Darker pixels count as text (or picture).
const double MIN_TEXT = 0.002;               // Share of text pixels beside pictures for a caption.
const double MAX_TEXT = 0.4;                 // More is a dark page, not text.

struct PictureMeasurement {
  double share = 0;
  double largest = 0;
  double colorInPictures = 0;
  double colorOutside = 0;
  double textOutside = 0;
};

// A pixel counts as colored when its channels differ clearly (not gray) and it is not nearly black.
bool isColored(const QRgb rgb) {
  const int r = qRed(rgb);
  const int g = qGreen(rgb);
  const int b = qBlue(rgb);
  const int mx = std::max({r, g, b});
  const int mn = std::min({r, g, b});
  return (mx - mn >= 40) && (mx >= 60);
}

PictureMeasurement measurePictures(const QImage& content, const TaskStatus& status) {
  PictureMeasurement result;
  const double dotsPerMeter300 = 300 / 0.0254;
  const double dpm = (content.dotsPerMeterX() > 0) ? content.dotsPerMeterX() : dotsPerMeter300;
  const double scale = std::min(1.0, dotsPerMeter300 / dpm);
  const QSize size300(std::max(1, qRound(content.width() * scale)), std::max(1, qRound(content.height() * scale)));

  const GrayImage gray300(scaleToGray(GrayImage(content), size300));
  // Light areas are pictures; BinaryImage stores them as WHITE, so invert to count them as black.
  const BinaryImage pictureMask
      = BinaryImage(detectPictures(gray300, false, status).toQImage(), BinaryThreshold(48)).inverted();
  status.throwIfCancelled();

  const double total = double(pictureMask.width()) * pictureMask.height();
  result.share = pictureMask.countBlackPixels() / total;

  const ConnectivityMap cmap(pictureMask, CONN8);
  std::vector<int> areas(cmap.maxLabel() + 1, 0);
  const uint32_t* line = cmap.data();
  for (int y = 0; y < cmap.size().height(); ++y, line += cmap.stride()) {
    for (int x = 0; x < cmap.size().width(); ++x) {
      ++areas[line[x]];
    }
  }
  for (size_t label = 1; label < areas.size(); ++label) {
    result.largest = std::max(result.largest, areas[label] / total);
  }

  // Dark pixels outside the pictures: text such as captions.
  {
    long text = 0;
    const uint32_t* maskLine = pictureMask.data();
    const int maskWpl = pictureMask.wordsPerLine();
    const uint32_t msb = uint32_t(1) << 31;
    for (int y = 0; y < gray300.height(); ++y, maskLine += maskWpl) {
      const uint8_t* grayLine = gray300.data() + y * gray300.stride();
      for (int x = 0; x < gray300.width(); ++x) {
        if ((grayLine[x] < TEXT_GRAY) && !(maskLine[x >> 5] & (msb >> (x & 31)))) {
          ++text;
        }
      }
    }
    result.textOutside = text / total;
  }

  if (!content.allGray()) {
    const QImage color300 = content.scaled(size300, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                                .convertToFormat(QImage::Format_RGB32);
    long inPictures = 0;
    long outside = 0;
    const uint32_t* maskLine = pictureMask.data();
    const int maskWpl = pictureMask.wordsPerLine();
    const uint32_t msb = uint32_t(1) << 31;
    for (int y = 0; y < color300.height(); ++y, maskLine += maskWpl) {
      const auto* colorLine = reinterpret_cast<const QRgb*>(color300.constScanLine(y));
      for (int x = 0; x < color300.width(); ++x) {
        if (isColored(colorLine[x])) {
          ++((maskLine[x >> 5] & (msb >> (x & 31))) ? inPictures : outside);
        }
      }
    }
    result.colorInPictures = inPictures / total;
    result.colorOutside = outside / total;
  }
  return result;
}

ColorModeDetector::Mode toMode(const LeptonicaDetector::ColorType type) {
  switch (type) {
    case LeptonicaDetector::ColorType::BlackWhite:
      return ColorModeDetector::Mode::BlackWhite;
    case LeptonicaDetector::ColorType::Grayscale:
      return ColorModeDetector::Mode::Grayscale;
    case LeptonicaDetector::ColorType::Mixed:
      return ColorModeDetector::Mode::Mixed;
    case LeptonicaDetector::ColorType::Color:
      return ColorModeDetector::Mode::Color;
  }
  return ColorModeDetector::Mode::BlackWhite;
}
}  // namespace

ColorModeDetector::Mode ColorModeDetector::detect(const QImage& content,
                                                  const Options& options,
                                                  const TaskStatus& status,
                                                  Evidence* evidence) {
  Evidence localEvidence;
  Evidence& ev = evidence ? *evidence : localEvidence;
  ev = Evidence();

  const LeptonicaDetector::ColorType spectreVerdict = LeptonicaDetector::detectWithCastCompensation(
      content, 10, &ev.spectre, LeptonicaDetector::AnalysisScalePolicy::productionDefault(), &status);
  ev.spectreVerdict = spectreVerdict;
  Mode mode = toMode(spectreVerdict);
  float darkRatio = ev.spectre.darkRatio;
  ev.reason = QStringLiteral("spectre");

  PictureMeasurement pictures;
  auto ensurePictures = [&]() {
    if (!ev.picturesMeasured) {
      pictures = measurePictures(content, status);
      ev.picturesMeasured = true;
      ev.pictureShare = pictures.share;
      ev.largestPicture = pictures.largest;
      ev.colorInPictures = pictures.colorInPictures;
      ev.colorOutside = pictures.colorOutside;
      ev.textOutside = pictures.textOutside;
    }
  };

  if (mode == Mode::Color) {
    ensurePictures();
    if (pictures.colorOutside < options.minColorShare) {
      if (pictures.colorInPictures >= MIN_COLOR_IN_PICTURES) {
        mode = Mode::Mixed;
        ev.reason = QStringLiteral("color_only_in_pictures");
      } else {
        // Too little color for a color page: judge the page again without color.
        LeptonicaDetector::DetectionEvidence grayEvidence;
        mode = toMode(LeptonicaDetector::detect(content.convertToFormat(QImage::Format_Grayscale8), 10, &grayEvidence,
                                                LeptonicaDetector::AnalysisScalePolicy::productionDefault(), &status));
        darkRatio = grayEvidence.darkRatio;
        ev.reason = QStringLiteral("too_little_color");
      }
    }
  }

  if (mode == Mode::Grayscale) {
    // A picture with text beside it, such as a caption: the text should be black and white.
    ensurePictures();
    if ((pictures.textOutside >= MIN_TEXT) && (pictures.textOutside < MAX_TEXT)) {
      mode = Mode::Mixed;
      ev.reason += QStringLiteral("+text_beside_picture");
    }
  }

  if (options.usePictureZones && (mode == Mode::BlackWhite)) {
    ensurePictures();
    if ((pictures.largest >= MIN_PICTURE) && (pictures.largest < MAX_PICTURE) && (darkRatio >= MIN_DARK)) {
      mode = Mode::Mixed;
      ev.reason += QStringLiteral("+picture_zone");
    }
  }
  return mode;
}

const char* ColorModeDetector::modeToString(const Mode mode) {
  switch (mode) {
    case Mode::BlackWhite:
      return "bw";
    case Mode::Grayscale:
      return "grayscale";
    case Mode::Mixed:
      return "mixed";
    case Mode::Color:
      return "color";
  }
  return "";
}
