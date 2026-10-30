// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F6: reading profile files, the default parameters of new pages (.stp, older ones .xml). The
// input is the file. Seeds: corpus/profile. Dictionary: profile.dict.
//
// A profile that is read is written back and read again, as when it is saved and loaded.

#include <QBuffer>
#include <QDomDocument>

#include "DefaultParams.h"
#include "DefaultParamsProfileManager.h"
#include "FuzzSetup.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  QByteArray input = fuzz::bytes(data, size);
  QBuffer buffer(&input);
  if (!buffer.open(QIODevice::ReadOnly)) {
    return 0;
  }
  const std::unique_ptr<DefaultParams> params = DefaultParamsProfileManager::readProfile(buffer);
  if (!params) {
    return 0;
  }
  QDomDocument doc;
  doc.appendChild(params->toXml(doc, "default-params"));
  (void) DefaultParams(doc.documentElement());
  return 0;
}
