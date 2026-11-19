// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ZONEMODELISTENER_H_
#define SCANTAILOR_CORE_ZONEMODELISTENER_H_

#include <core/zones/ZoneCreationMode.h>

#include <functional>

class ZoneModeListener {
 public:
  virtual ~ZoneModeListener() = default;

  /**
   * Called when a zone editor starts providing its mode. \p setMode lets the
   * listener change the editor's mode; it stays valid until onZoneModeProviderStopped().
   */
  virtual void onZoneModeProviderStarted(const std::function<void(ZoneCreationMode)>& /*setMode*/) {}

  virtual void onZoneModeChanged(ZoneCreationMode mode) = 0;

  virtual void onZoneModeProviderStopped() = 0;
};


#endif  // SCANTAILOR_CORE_ZONEMODELISTENER_H_
