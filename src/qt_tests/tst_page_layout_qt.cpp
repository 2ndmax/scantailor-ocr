// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_page_layout_qt.h"

#include <ImageInfo.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <filters/page_layout/OptionsWidget.h>
#include <filters/page_layout/Settings.h>

#include <QtTest/QtTest>
#include <memory>
#include <vector>

#include "length_field_checks.h"

void TestPageLayoutQt::testLengthFieldsSetDecimalsStepAndUnit() {
  const struct {
    Units units;
    int decimals;
    double step;
  } formats[] = {{PIXELS, 0, 1.0}, {MILLIMETRES, 1, 1.0}, {CENTIMETRES, 2, 0.01}, {INCHES, 2, 0.01}};
  auto pages = std::make_shared<ProjectPages>(std::vector<ImageInfo>(), Qt::LeftToRight);
  page_layout::OptionsWidget widget(std::make_shared<page_layout::Settings>(), pages, PageSelectionAccessor(nullptr));
  for (const auto& format : formats) {
    const ScopedUnits scopedUnits(format.units);
    for (const char* name : {"topMarginSpinBox", "bottomMarginSpinBox", "leftMarginSpinBox", "rightMarginSpinBox",
                             "targetWidthSpinBox", "targetHeightSpinBox"}) {
      QVERIFY2(lengthFieldShows(widget.findChild<QDoubleSpinBox*>(name), format.units, format.decimals, format.step),
               name);
    }
  }
}
