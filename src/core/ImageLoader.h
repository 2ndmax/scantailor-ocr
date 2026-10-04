// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_IMAGELOADER_H_
#define SCANTAILOR_CORE_IMAGELOADER_H_

#include <QSize>
#include <QStringList>

class ImageId;
class QImage;
class QString;
class QIODevice;

class ImageLoader {
 public:
  static QImage load(const QString& filePath, int pageNum = 0);

  /**
   * \brief Loads a source image of the project.
   *
   * Unlike the other overloads, failures are reported to ImageLoadErrorReporter,
   * so the user gets to know why an image couldn't be loaded.
   *
   * \param errorMessages If not null, receives the reasons of a failure.
   */
  static QImage load(const ImageId& imageId, QStringList* errorMessages = nullptr);

  /**
   * \brief Like load(const ImageId&), but allows the image to be loaded at a reduced
   *        resolution, as long as it's still at least \p minSize large.
   *
   * Only formats that can decode reduced resolutions cheaply (JPEG 2000 and JPEG) make
   * use of that, the others are loaded at full resolution.  Meant for thumbnails.
   */
  static QImage loadForThumbnail(const ImageId& imageId, const QSize& minSize);

  static QImage load(QIODevice& ioDev, int pageNum, const QSize& minSize = QSize());

 private:
  static QImage loadReportingErrors(const ImageId& imageId, const QSize& minSize, QStringList* errorMessages);
};


#endif
