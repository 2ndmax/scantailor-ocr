// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageMetadataLoader.h"

#include <QFile>
#include <QIODevice>
#include <QString>
#include <vector>

#include "ImageLoadErrors.h"
#include "ImageMetadata.h"
#include "ImageSizeLimit.h"
#include "Jp2MetadataLoader.h"
#include "JpegMetadataLoader.h"
#include "PngMetadataLoader.h"
#include "TiffMetadataLoader.h"

ImageMetadataLoader::LoaderList ImageMetadataLoader::m_sLoaders;

void ImageMetadataLoader::registerLoader(std::shared_ptr<ImageMetadataLoader> loader) {
  m_sLoaders.push_back(std::move(loader));
}

ImageMetadataLoader::StaticInit::StaticInit() {
  registerLoader(std::make_shared<JpegMetadataLoader>());
  registerLoader(std::make_shared<PngMetadataLoader>());
  registerLoader(std::make_shared<TiffMetadataLoader>());
  registerLoader(std::make_shared<Jp2MetadataLoader>());
}

ImageMetadataLoader::StaticInit ImageMetadataLoader::m_staticInit;

ImageMetadataLoader::Status ImageMetadataLoader::loadImpl(QIODevice& ioDevice,
                                                          const VirtualFunction<void, const ImageMetadata&>& out) {
  // The pages are passed on only once all of them turned out to be within the size limit: a file
  // with a page that can't be loaded later is refused as a whole, while the user is still adding
  // files. Taking only the other pages would number the pages differently from the file.
  std::vector<ImageMetadata> pages;
  auto collect = [&](const ImageMetadata& metadata) { pages.push_back(metadata); };
  const ProxyFunction<decltype(collect), void, const ImageMetadata&> collectProxy(collect);

  Status status = FORMAT_NOT_RECOGNIZED;
  for (const auto& loader : m_sLoaders) {
    status = loader->loadMetadata(ioDevice, collectProxy);
    if (status != FORMAT_NOT_RECOGNIZED) {
      break;
    }
  }
  if (status != LOADED) {
    return status;
  }

  for (const ImageMetadata& page : pages) {
    if (ImageSizeLimit::exceeds(page.size())) {
      ImageLoadErrorCapture::addError(ImageSizeLimit::errorMessage(page.size()));
      return GENERIC_ERROR;
    }
  }
  for (const ImageMetadata& page : pages) {
    out(page);
  }
  return LOADED;
}

ImageMetadataLoader::Status ImageMetadataLoader::loadImpl(const QString& filePath,
                                                          const VirtualFunction<void, const ImageMetadata&>& out) {
  QFile file(filePath);
  if (!file.open(QIODevice::ReadOnly)) {
    return GENERIC_ERROR;
  }
  return loadImpl(file, out);
}
