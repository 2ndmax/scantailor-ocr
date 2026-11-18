// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "UiData.h"

namespace deskew {
UiData::UiData()
    : m_effDeskewAngle(0.0),
      m_effObliqueAngle(0.0),
      m_mode(MODE_AUTO),
      m_obliqueMode(MODE_MANUAL),
      m_detection(DETECT_CONTENT) {}

UiData::~UiData() = default;
}  // namespace deskew
