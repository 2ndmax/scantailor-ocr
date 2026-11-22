// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageFileInfo.h"

#include <algorithm>

#include <QCoreApplication>

#include "ImageLoadErrors.h"
#include "ImageMetadataLoader.h"

bool ImageFileInfo::isDpiOK() const {
  return std::find_if(m_imageInfo.begin(), m_imageInfo.end(),
                      [](const ImageMetadata& metadata) { return !metadata.isDpiOK(); })
         == m_imageInfo.end();
}

std::optional<ImageFileInfo> ImageFileInfo::load(const QFileInfo& fileInfo,
                                                 QStringList* loadErrors,
                                                 QStringList* partialReads) {
  std::vector<ImageMetadata> metadata;
  ImageLoadErrorCapture capture;
  const ImageMetadataLoader::Status status = ImageMetadataLoader::load(
      fileInfo.absoluteFilePath(), [&metadata](const ImageMetadata& image) { metadata.push_back(image); });
  if (partialReads != nullptr) {
    *partialReads = capture.partialReads();
  }
  if (status != ImageMetadataLoader::LOADED) {
    if (loadErrors != nullptr) {
      QStringList reasons = capture.messages();
      if (reasons.isEmpty()) {
        reasons.push_back(
            QCoreApplication::translate("ImageLoader", "The file format is not supported, or the file is damaged."));
      }
      *loadErrors = reasons;
    }
    return std::nullopt;
  }
  return ImageFileInfo(fileInfo, metadata);
}
