// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageSettings.h"

#include "AbstractRelinker.h"
#include "RelinkablePath.h"
#include "Utils.h"

using namespace core;
using namespace imageproc;

namespace {
// Used when nothing better is known; the middle of the gray range.
const int kDefaultBwThreshold = 128;

/**
 * A missing, malformed or out-of-range threshold must not silently be used: a value
 * below 0 or above 256 would white out or black out the whole page.
 */
BinaryThreshold readBwThreshold(const QDomElement& el) {
  bool ok = false;
  const int threshold = el.attribute("bwThreshold").toInt(&ok);
  if (!ok || (threshold < 0) || (threshold > 256)) {
    return BinaryThreshold(kDefaultBwThreshold);
  }
  return BinaryThreshold(threshold);
}
}  // namespace

void ImageSettings::clear() {
  QMutexLocker locker(&m_mutex);
  m_perPageParams.clear();
}

void ImageSettings::performRelinking(const AbstractRelinker& relinker) {
  QMutexLocker locker(&m_mutex);
  PerPageParams newParams;
  for (const auto& kv : m_perPageParams) {
    const RelinkablePath oldPath(kv.first.imageId().filePath(), RelinkablePath::File);
    PageId newPageId(kv.first);
    newPageId.imageId().setFilePath(relinker.substitutionPathFor(oldPath));
    newParams.insert(PerPageParams::value_type(newPageId, kv.second));
  }

  m_perPageParams.swap(newParams);
}

void ImageSettings::setPageParams(const PageId& pageId, const PageParams& params) {
  QMutexLocker locker(&m_mutex);
  Utils::mapSetValue(m_perPageParams, pageId, params);
}

std::unique_ptr<ImageSettings::PageParams> ImageSettings::getPageParams(const PageId& pageId) const {
  QMutexLocker locker(&m_mutex);
  const auto it(m_perPageParams.find(pageId));
  if (it != m_perPageParams.end()) {
    return std::make_unique<PageParams>(it->second);
  } else {
    return nullptr;
  }
}

/*=============================== ImageSettings::Params ==================================*/

ImageSettings::PageParams::PageParams() : m_bwThreshold(kDefaultBwThreshold), m_blackOnWhite(true) {}

ImageSettings::PageParams::PageParams(const BinaryThreshold& bwThreshold, bool blackOnWhite)
    : m_bwThreshold(bwThreshold), m_blackOnWhite(blackOnWhite) {}

ImageSettings::PageParams::PageParams(const QDomElement& el)
    : m_bwThreshold(readBwThreshold(el)),
      // Project files written elsewhere may not carry the attribute at all. Defaulting to false
      // would make us treat an ordinary scan as white-on-black.
      m_blackOnWhite(el.attribute("blackOnWhite", "1") != "0") {}

QDomElement ImageSettings::PageParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.setAttribute("bwThreshold", m_bwThreshold);
  el.setAttribute("blackOnWhite", m_blackOnWhite ? "1" : "0");
  return el;
}