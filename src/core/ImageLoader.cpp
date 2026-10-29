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
#include "ImageSizeLimit.h"
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
  QString errorString;
  if (readWithQt(ioDev, minSize, nullptr, image, errorString)) {
    return image;
  }
  // Issue #98: on some builds Qt may not detect JPG format; force it for .jpg/.jpeg files.
  // Forcing is only the second try, because the JPEG plugin accepts any content when forced,
  // so a PNG that is named .jpg would fail.
  if (const QFile* file = qobject_cast<QFile*>(&ioDev)) {
    const QString ext = QFileInfo(file->fileName()).suffix().toLower();
    if ((ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg")) && ioDev.seek(0)) {
      QImage forcedImage;
      QString forcedErrorString;
      if (readWithQt(ioDev, minSize, "jpeg", forcedImage, forcedErrorString)) {
        return forcedImage;
      }
      errorString = forcedErrorString;
    }
  }
  ImageLoadErrorCapture::addError(errorString);
  return QImage();
}

bool ImageLoader::readWithQt(QIODevice& ioDev,
                             const QSize& minSize,
                             const char* forcedFormat,
                             QImage& image,
                             QString& errorString) {
  QImageReader reader(&ioDev);
  reader.setAutoTransform(true);  // Helps with orientation and some formats (issue #20).
  if (forcedFormat) {
    reader.setFormat(forcedFormat);
  }
  // The size from the header, before decoding. A format that can't tell it is left to the
  // allocation limit of QImageReader (see main.cpp).
  const QSize fullSize = reader.size();
  if (ImageSizeLimit::exceeds(fullSize)) {
    errorString = ImageSizeLimit::errorMessage(fullSize);
    return false;
  }
  if (minSize.isValid() && !minSize.isEmpty() && (reader.format() == "jpeg")) {
    // libjpeg can decode directly at 1/2, 1/4 or 1/8 of the size, which is
    // much faster than decoding everything and scaling down afterwards.
    // The size refers to the image as stored, before applying the EXIF
    // orientation, so it has to be large enough in either orientation.
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
    errorString = reader.errorString();
    return false;
  }
  return true;
}
