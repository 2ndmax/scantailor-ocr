// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ImageViewInfoProvider.h"

#include <algorithm>

#include "ImageViewInfoListener.h"

ImageViewInfoProvider::ImageViewInfoProvider(const Dpi& dpi) : m_dpi(dpi) {}

ImageViewInfoProvider::~ImageViewInfoProvider() {
  notifyProviderStopped();
}

void ImageViewInfoProvider::addListener(ImageViewInfoListener* listener) {
  if (!listener) {
    return;
  }
  if (std::find(m_listeners.begin(), m_listeners.end(), listener) != m_listeners.end()) {
    return;
  }
  listener->onDpiChanged(m_dpi);
  listener->onPhysSizeChanged(m_physSize);
  listener->onMousePosChanged(m_mousePos);

  m_listeners.push_back(listener);
}

void ImageViewInfoProvider::removeListener(ImageViewInfoListener* listener) {
  if (!listener) {
    return;
  }
  const auto it = std::find(m_listeners.begin(), m_listeners.end(), listener);
  if (it != m_listeners.end()) {
    (*it)->onImageViewInfoProviderStopped();
    m_listeners.erase(it);
  }
}

void ImageViewInfoProvider::removeAllListeners() {
  notifyProviderStopped();
  m_listeners.clear();
}

void ImageViewInfoProvider::setPhysSize(const QSizeF& physSize) {
  ImageViewInfoProvider::m_physSize = physSize;
  physSizeChanged(physSize);
}

void ImageViewInfoProvider::setMousePos(const QPointF& mousePos) {
  ImageViewInfoProvider::m_mousePos = mousePos;
  mousePosChanged(mousePos);
}

void ImageViewInfoProvider::physSizeChanged(const QSizeF& physSize) const {
  for (ImageViewInfoListener* listener : m_listeners) {
    listener->onPhysSizeChanged(physSize);
  }
}

void ImageViewInfoProvider::mousePosChanged(const QPointF& mousePos) const {
  for (ImageViewInfoListener* listener : m_listeners) {
    listener->onMousePosChanged(mousePos);
  }
}

const Dpi& ImageViewInfoProvider::getDpi() const {
  return m_dpi;
}

const QPointF& ImageViewInfoProvider::getMousePos() const {
  return m_mousePos;
}

const QSizeF& ImageViewInfoProvider::getPhysSize() const {
  return m_physSize;
}

void ImageViewInfoProvider::notifyProviderStopped() const {
  for (ImageViewInfoListener* listener : m_listeners) {
    listener->onImageViewInfoProviderStopped();
  }
}
