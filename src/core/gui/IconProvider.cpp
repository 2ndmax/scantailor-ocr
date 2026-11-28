// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "IconProvider.h"

#include "IconPack.h"

namespace {
IconProvider*& overrideInstance() {
  static IconProvider* overridePtr = nullptr;
  return overridePtr;
}
}  // namespace

IconProvider::ScopedOverride::ScopedOverride(IconProvider& replacement) : m_previous(overrideInstance()) {
  overrideInstance() = &replacement;
}

IconProvider::ScopedOverride::~ScopedOverride() {
  overrideInstance() = m_previous;
}

IconProvider::IconProvider() = default;

IconProvider::~IconProvider() = default;

IconProvider& IconProvider::getInstance() {
  if (IconProvider* overridePtr = overrideInstance()) {
    return *overridePtr;
  }
  static IconProvider instance;
  return instance;
}

void IconProvider::setIconPack(std::unique_ptr<IconPack> pack) {
  m_iconPack = std::move(pack);
}

void IconProvider::addIconPack(std::unique_ptr<IconPack> pack) {
  if (pack) {
    pack->mergeWith(std::move(m_iconPack));
    m_iconPack = std::move(pack);
  }
}

void IconProvider::reset() {
  m_iconPack.reset();
}

QIcon IconProvider::getIcon(const QString& iconKey) const {
  return m_iconPack ? m_iconPack->getIcon(iconKey) : QIcon();
}
