// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F3: reading the per-page settings of the filters from a project file. The first byte of the
// input chooses the class, the rest is its XML element. Seeds: corpus/filter_params, their file
// names start with the class number. Dictionary: filter_params.dict.
//
// Each element is read, written back and read again, as when a project is saved and reopened.

#include <QDomDocument>
#include <functional>
#include <iterator>

#include "FuzzSetup.h"
#include "filters/deskew/Dependencies.h"
#include "filters/deskew/Params.h"
#include "filters/output/OutputParams.h"
#include "filters/output/Params.h"
#include "filters/page_layout/Guide.h"
#include "filters/page_layout/Params.h"
#include "filters/page_split/Dependencies.h"
#include "filters/page_split/Params.h"
#include "filters/select_content/Dependencies.h"
#include "filters/select_content/Params.h"

namespace {
template <typename T>
void readWriteRead(const QDomElement& el) {
  QDomDocument doc;
  doc.appendChild(T(el).toXml(doc, "settings"));
  (void) T(doc.documentElement());
}

// The order is the class number of the seeds; append new classes at the end.
const std::function<void(const QDomElement&)> readers[] = {
    readWriteRead<deskew::Params>,                // 0
    readWriteRead<deskew::Dependencies>,          // 1
    readWriteRead<page_split::Params>,            // 2
    readWriteRead<page_split::Dependencies>,      // 3
    readWriteRead<select_content::Params>,        // 4
    readWriteRead<select_content::Dependencies>,  // 5
    readWriteRead<page_layout::Params>,           // 6
    readWriteRead<page_layout::Guide>,            // 7
    readWriteRead<output::Params>,                // 8
    readWriteRead<output::OutputParams>,          // 9
};
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  if (size < 1) {
    return 0;
  }
  QDomDocument doc;
  if (!doc.setContent(fuzz::bytes(data + 1, size - 1))) {
    return 0;
  }
  readers[data[0] % std::size(readers)](doc.documentElement());
  return 0;
}
