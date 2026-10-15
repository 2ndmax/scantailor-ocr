// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include <core/DefaultParams.h>
#include <core/DefaultParamsProvider.h>

#include "ImageTransformation.h"
#include "ImageTrim.h"

using namespace fix_orientation;

OrthogonalRotation Utils::getDefaultOrthogonalRotation() {
  const DefaultParams& defaultParams = DefaultParamsProvider::getInstance().getParams();
  const DefaultParams::FixOrientationParams& fixOrientationParams = defaultParams.getFixOrientationParams();

  return fixOrientationParams.getImageRotation();
}

void Utils::applyRotationAndTrim(ImageTransformation& xform,
                                 const OrthogonalRotation& rotation,
                                 const ImageTrim& trim) {
  xform.setPreRotation(rotation);
  if (trim.enabled) {
    const QRect inner(trim.toInnerRect(xform.origRect().size().toSize()));
    xform.setPreCropArea(xform.origRectToPreCropSpace(QRectF(inner)));
  }
}
