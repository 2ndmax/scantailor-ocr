// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_QT_TESTS_TEST_PROJECT_H_
#define SCANTAILOR_QT_TESTS_TEST_PROJECT_H_

#include <DefaultParams.h>
#include <Dpm.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <algorithm>
#include <vector>

/**
 * Scans drawn in the test and a project set up from them as the app sets it up, without its window.
 */
namespace test_pages {
inline constexpr int DPI = 300;
inline constexpr double SKEW_DEG = 1.5;
inline const QSize PAGE_SIZE(1200, 1700);
inline const QRect TEXT_RECT(200, 250, 800, 1200);

// Lines of "words" of different lengths, like printed text.
inline void drawText(QPainter& painter, const QRect& rect) {
  for (int y = rect.top(); y + 6 <= rect.bottom(); y += 18) {
    int x = rect.left();
    while (true) {
      const int width = 20 + ((x * 7 + y * 3) % 40);
      if (x + width > rect.right()) {
        break;
      }
      painter.fillRect(QRect(x, y, width, 6), Qt::black);
      x += width + 12;
    }
  }
}

inline QImage blankImage(const QSize& size) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(qRgb(250, 250, 250));
  const Dpm dpm(Dpi(DPI, DPI));
  image.setDotsPerMeterX(dpm.horizontal());
  image.setDotsPerMeterY(dpm.vertical());
  return image;
}

// A page with text turned by SKEW_DEG and a dark border all around, as from a scanner lid.
inline QImage skewedPage() {
  QImage image(blankImage(PAGE_SIZE));
  QPainter painter(&image);
  painter.fillRect(image.rect(), qRgb(40, 40, 40));
  painter.fillRect(image.rect().adjusted(60, 60, -60, -60), qRgb(250, 250, 250));
  const QPointF center(image.rect().center());
  painter.translate(center);
  painter.rotate(SKEW_DEG);
  painter.translate(-center);
  drawText(painter, TEXT_RECT);
  return image;
}

// Two pages side by side with a dark gutter between them.
inline QImage twoPageSpread() {
  QImage image(blankImage(QSize(PAGE_SIZE.width() * 2, PAGE_SIZE.height())));
  QPainter painter(&image);
  drawText(painter, TEXT_RECT);
  drawText(painter, TEXT_RECT.translated(PAGE_SIZE.width(), 0));
  painter.fillRect(QRect(PAGE_SIZE.width() - 10, 0, 20, PAGE_SIZE.height()), qRgb(60, 60, 60));
  return image;
}

// A colored picture above black text. The picture has edges like a photo; a smooth gradient
// would count as background, not as content.
inline QImage colorPage() {
  QImage image(blankImage(PAGE_SIZE));
  const QRect pictureRect(TEXT_RECT.left(), TEXT_RECT.top(), TEXT_RECT.width(), 500);
  QPainter painter(&image);
  constexpr int tile = 25;
  for (int y = pictureRect.top(); y < pictureRect.bottom(); y += tile) {
    for (int x = pictureRect.left(); x < pictureRect.right(); x += tile) {
      const int v = 40 + ((x * 13 + y * 7) % 170);
      painter.fillRect(QRect(x, y, tile, tile), QColor(v, 220 - v, 60 + (v * 3) % 150));
    }
  }
  drawText(painter, QRect(TEXT_RECT.left(), pictureRect.bottom() + 100, TEXT_RECT.width(), 600));
  return image;
}

inline ImageFileInfo saveScan(const QImage& image, const QString& filePath) {
  if (!image.save(filePath)) {
    return ImageFileInfo(QFileInfo(filePath), {});
  }
  return ImageFileInfo(QFileInfo(filePath), {ImageMetadata(image.size(), Dpi(DPI, DPI))});
}

// The "Project Files" dialog creates the output folder before the project is set up.
inline const QString& createdDir(const QString& dir) {
  QDir().mkpath(dir);
  return dir;
}

// A new project set up as the app sets it up, without its window.
class TestProject : public ProjectSession {
 public:
  TestProject(const std::vector<ImageFileInfo>& files, const QString& outDir, const DefaultParams& startValues)
      : ProjectSession(
            ProjectSession::createNew(ProjectPages::createNew(files, startValues.getPageSplitParams().getPageOrder()),
                                      createdDir(outDir),
                                      QString(),
                                      startValues)) {}

  /**
   * All six steps, as a command line tool would run them. Returns false if a page failed or
   * nothing was output; the caller checks it, as a QVERIFY here wouldn't end the test.
   */
  [[nodiscard]] bool processAll() {
    const StageSequence::ProcessAllResult result = stages->processAll(thumbnailCache, outFileNameGen);
    // Which page failed and why, as QVERIFY only shows that one did.
    for (const StageSequence::PageFailure& failure : result.failures) {
      qWarning().noquote() << "Page failed:" << failure.page.imageId().filePath() << "sub page"
                           << failure.page.subPage() << "kind" << failure.kind << failure.reasons.join("; ");
    }
    if (!result.output) {
      qWarning() << "No output.";
    }
    return result.failures.empty() && result.output;
  }
};

// The three scans of the test project: skewed page, two-page spread, color page.
inline std::vector<ImageFileInfo> saveScans(const QDir& dir) {
  return {saveScan(skewedPage(), dir.absoluteFilePath("1-skewed.png")),
          saveScan(twoPageSpread(), dir.absoluteFilePath("2-spread.png")),
          saveScan(colorPage(), dir.absoluteFilePath("3-color.png"))};
}

// Saving a scan fails e.g. without the PNG plugin; then the project has images without a size.
inline bool allSaved(const std::vector<ImageFileInfo>& files) {
  return std::all_of(files.begin(), files.end(), [](const ImageFileInfo& file) { return !file.imageInfo().empty(); });
}
}  // namespace test_pages

#endif  // SCANTAILOR_QT_TESTS_TEST_PROJECT_H_
