// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_TESTS_TESTHELPERS_H_
#define SCANTAILOR_CORE_TESTS_TESTHELPERS_H_

#include <AbstractRelinker.h>
#include <Dpi.h>
#include <Dpm.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <RelinkablePath.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QString>

namespace Tests {
/** The content of a file, or an empty array if it cannot be read. */
inline QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/** Writes a file, creating its folder if needed. Returns false unless everything was written. */
inline bool writeFile(const QString& path, const QByteArray& content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && (file.write(content) == content.size());
}

/** A white black-and-white image: index 0 is white, index 1 is black. */
inline QImage whiteMonoImage(const int width, const int height) {
  QImage image(width, height, QImage::Format_Mono);
  image.setColorTable({qRgb(255, 255, 255), qRgb(0, 0, 0)});
  image.fill(0);
  return image;
}

/**
 * Saves a scanned page as a project takes it: white, with a block of black bars for the lines of
 * text, as drawing text itself would need a QGuiApplication. With \p dpi 0, the resolution is
 * missing, as with some scans. Returns the file as the project lists it; check its imageInfo(),
 * which is empty if saving failed.
 */
inline ImageFileInfo saveTextPageScan(const QString& path, const int dpi) {
  QImage page(1200, 1600, QImage::Format_RGB32);
  page.fill(Qt::white);
  {
    QPainter painter(&page);
    for (int y = 300; y < 1300; y += 60) {
      painter.fillRect(QRect(200, y, 800, 25), Qt::black);
    }
  }
  if (dpi > 0) {
    const Dpm dpm(Dpi(dpi, dpi));
    page.setDotsPerMeterX(dpm.horizontal());
    page.setDotsPerMeterY(dpm.vertical());
  }
  if (!page.save(path)) {
    return ImageFileInfo(QFileInfo(path), {});
  }
  return ImageFileInfo(QFileInfo(path), {ImageMetadata(page.size(), Dpi(dpi, dpi))});
}

inline bool isBlack(const QImage& image, const int x, const int y) {
  return qGray(image.pixel(x, y)) < 128;
}

/** Substitutes one file path, as if the user had relinked a moved file. */
class RenamingRelinker : public AbstractRelinker {
 public:
  RenamingRelinker(const QString& from, const QString& to) : m_from(from), m_to(to) {}

  QString substitutionPathFor(const RelinkablePath& origPath) const override {
    return (origPath.normalizedPath() == m_from) ? m_to : origPath.normalizedPath();
  }

 private:
  QString m_from;
  QString m_to;
};
}  // namespace Tests

#endif  // SCANTAILOR_CORE_TESTS_TESTHELPERS_H_
