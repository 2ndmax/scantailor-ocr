// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include "DeskewParams.h"

#include "Params.h"

using namespace deskew;

Params Utils::buildDefaultParams(const DeskewParams& deskewParams) {
  return Params(deskewParams.getDeskewAngleDeg(), 0.0, Dependencies(), deskewParams.getMode(),
                deskewParams.isAutoOblique() ? MODE_AUTO : MODE_MANUAL, deskewParams.getDetection());
}
