// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageLoader.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStringList>
#include <QtGui/QImageReader>
#include <algorithm>
#include <cmath>

#include "ImageId.h"
#include "ImageLoadErrors.h"
#include "Jp2Reader.h"
#include "TiffReader.h"

QImage ImageLoader::load(const ImageId& imageId, QStringList* errorMessages) {
  return loadReportingErrors(imageId, QSize(), errorMessages);
}

QImage ImageLoader::loadForThumbnail(const ImageId& imageId, const QSize& minSize) {
  return loadReportingErrors(imageId, minSize, nullptr);
}

QImage ImageLoader::loadReportingErrors(const ImageId& imageId, const QSize& minSize, QStringList* errorMessages) {
  const QString& filePath = imageId.filePath();

  QImage image;
  QStringList messages;
  {
    ImageLoadErrorCapture capture;
    QFile file(filePath);
    if (file.open(QIODevice::ReadOnly)) {
      image = load(file, imageId.zeroBasedPage(), minSize);
    } else if (file.exists()) {
      ImageLoadErrorCapture::addError(file.errorString());
    }
    messages = capture.messages();
  }

  if (image.isNull()) {
    if (messages.isEmpty()) {
      messages.push_back(
          QCoreApplication::translate("ImageLoader", "The file format is not supported, or the file is damaged."));
    }
    // A missing file is not reported here: the user gets offered the relinking tool for that.
    if (QFile::exists(filePath)) {
      ImageLoadErrorReporter::instance().report(filePath, imageId.page(), messages);
    }
    if (errorMessages) {
      *errorMessages = messages;
    }
  }
  return image;
}

QImage ImageLoader::load(const QString& filePath, const int pageNum) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return QImage();
  }
  return load(file, pageNum);
}

QImage ImageLoader::load(QIODevice& ioDev, const int pageNum, const QSize& minSize) {
  if (TiffReader::canRead(ioDev)) {
    return TiffReader::readImage(ioDev, pageNum);
  }

  if (pageNum != 0) {
    // Only TIFF supports multiple pages.
    return QImage();
  }

  if (Jp2Reader::canRead(ioDev)) {
    return Jp2Reader::readImage(ioDev, minSize);
  }

  QImage image;
  QImageReader reader(&ioDev);
  reader.setAutoTransform(true);  // Helps with orientation and some formats (issue #20).
  // Issue #98: on some builds Qt may not detect JPG format; force it for .jpg/.jpeg files.
  if (QFile* file = qobject_cast<QFile*>(&ioDev)) {
    const QString ext = QFileInfo(file->fileName()).suffix().toLower();
    if (ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) {
      reader.setFormat("jpeg");
    }
  }
  if (minSize.isValid() && !minSize.isEmpty() && (reader.format() == "jpeg")) {
    // libjpeg can decode directly at 1/2, 1/4 or 1/8 of the size, which is
    // much faster than decoding everything and scaling down afterwards.
    // The size refers to the image as stored, before applying the EXIF
    // orientation, so it has to be large enough in either orientation.
    const QSize fullSize = reader.size();
    if (!fullSize.isEmpty()) {
      const double minDim = std::max(minSize.width(), minSize.height());
      const double factor = minDim / std::min(fullSize.width(), fullSize.height());
      if (factor < 1.0) {
        reader.setScaledSize(QSize(std::max(1, static_cast<int>(std::ceil(fullSize.width() * factor))),
                                   std::max(1, static_cast<int>(std::ceil(fullSize.height() * factor)))));
      }
    }
  }
  if (!reader.read(&image)) {
    ImageLoadErrorCapture::addError(reader.errorString());
  }
  return image;
}
