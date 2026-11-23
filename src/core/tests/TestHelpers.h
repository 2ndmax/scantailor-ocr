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
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QString>

#include <map>
#include <set>
#include <utility>

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

/**
 * Draws a page of "text" as black bars with ascenders and descenders, rotated
 * by \p rotation degrees clockwise. Boxes are used instead of real glyphs because
 * drawing text needs a QGuiApplication and a font database, neither of which the
 * core tests have. \p dpi sets both the resolution metadata and the drawing
 * scale, so the same page can be produced at 300 or 600 dpi (twice as large).
 */
inline QImage makeTextPage(const int rotation, const int dpi = 300) {
  const int resolution = (dpi > 0) ? dpi : 300;
  const double s = resolution / 300.0;
  const int baseW = qRound(800 * s);
  const int baseH = qRound(2100 * s);
  QImage base(baseW, baseH, QImage::Format_RGB32);
  base.fill(Qt::white);
  {
    QPainter painter(&base);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    for (int line = 0; line < 45; ++line) {
      const int y = qRound((50 + line * 40) * s);
      for (int g = 0; g < 38; ++g) {
        const int x = qRound((60 + g * 18) * s);
        // stem (x-height)
        painter.fillRect(QRect(x, y - qRound(20 * s), qRound(3 * s), qRound(20 * s)), Qt::black);
        // ascender
        if (g % 2 == 0) {
          painter.fillRect(QRect(x, y - qRound(32 * s), qRound(3 * s), qRound(12 * s)), Qt::black);
        }
        // sparse crossbar for rotated signal
        if (g % 17 == 0) {
          painter.fillRect(QRect(x - qRound(2 * s), y - qRound(12 * s), qRound(8 * s), qRound(3 * s)),
                           Qt::black);
        }
      }
    }
  }

  QImage out;
  if (rotation == 0) {
    out = base;
  } else if (rotation == 180) {
    out = QImage(base.size(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(180);
    p.translate(-base.width() / 2.0, -base.height() / 2.0);
    p.drawImage(0, 0, base);
  } else {
    out = QImage(base.height(), base.width(), QImage::Format_RGB32);
    out.fill(Qt::white);
    QPainter p(&out);
    p.translate(out.width() / 2.0, out.height() / 2.0);
    p.rotate(rotation);
    p.translate(-base.width() / 2.0, -base.height() / 2.0);
    p.drawImage(0, 0, base);
  }

  const Dpm dpm(Dpi(resolution, resolution));
  out.setDotsPerMeterX(dpm.horizontal());
  out.setDotsPerMeterY(dpm.vertical());
  return out;
}

/** Alias for makeTextPage, kept for callers that prefer this name. */
inline QImage drawTextPage(const int rotation, const int dpi = 300) {
  return makeTextPage(rotation, dpi);
}

/** Alias for makeTextPage, kept for callers that prefer this name. */
inline QImage textPage(const int rotation, const int dpi = 300) {
  return makeTextPage(rotation, dpi);
}

/**
 * Saves a drawn text page as a project takes it, with the given rotation and
 * resolution. Returns the file as the project lists it.
 */
inline ImageFileInfo saveTextPage(const QString& path, const int rotation, const int dpi = 300) {
  const int resolution = (dpi > 0) ? dpi : 0;
  QImage page = makeTextPage(rotation, (resolution > 0) ? resolution : 300);
  if (resolution > 0) {
    const Dpm dpm(Dpi(resolution, resolution));
    page.setDotsPerMeterX(dpm.horizontal());
    page.setDotsPerMeterY(dpm.vertical());
  } else {
    // keep dpi 0 metadata (no resolution), as saveTextPageScan allows
    page.setDotsPerMeterX(0);
    page.setDotsPerMeterY(0);
  }
  if (!page.save(path)) {
    return ImageFileInfo(QFileInfo(path), {});
  }
  return ImageFileInfo(QFileInfo(path), {ImageMetadata(page.size(), Dpi(resolution, resolution))});
}

/** Overload of saveTextPageScan that also rotates the drawn page. */
inline ImageFileInfo saveTextPageScan(const QString& path, const int rotation, const int dpi) {
  return saveTextPage(path, rotation, dpi);
}

/** Alias for saveTextPage. */
inline ImageFileInfo saveRotatedTextPage(const QString& path, const int rotation, const int dpi = 300) {
  return saveTextPage(path, rotation, dpi);
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
/** Recursively snapshots \p root: relative path -> (lastModified for files, isDir). Directory
 * mtimes are recorded as a null time, so only the path set is compared for directories. */
inline std::map<QString, std::pair<QDateTime, bool>> snapshotDirectory(const QString& root) {
  std::map<QString, std::pair<QDateTime, bool>> entries;
  QDirIterator iterator(root, QDir::AllEntries | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
  while (iterator.hasNext()) {
    iterator.next();
    const QFileInfo info(iterator.fileInfo());
    const QString relative = QDir(root).relativeFilePath(info.absoluteFilePath());
    const QDateTime modified = info.isFile() ? info.lastModified() : QDateTime();
    entries[relative] = std::make_pair(modified, info.isDir());
  }
  return entries;
}

}  // namespace Tests

#endif  // SCANTAILOR_CORE_TESTS_TESTHELPERS_H_
