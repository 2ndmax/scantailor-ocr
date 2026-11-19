// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DewarpingMethodBox.h"

#include <QComboBox>
#include <QLabel>
#include <QStringList>

namespace output {
void DewarpingMethodBox::setUp(QComboBox* box, QLabel* label) {
  struct Method {
    DewarpingMode mode;
    QString name;
    QString description;
  };
  const Method methods[] = {
      {AUTO, tr("Auto"), tr("Finds the curvature of the lines of text automatically. Experimental.")},
      {TRAPEZOID, tr("Trapezoid"),
       tr("Takes the lines of text as straight and corrects only a trapezoid distortion of the page, as found on scans "
          "of flat pages. A page with fewer than 8 lines of text stays as it is. Experimental.")},
      {MARGINAL, tr("Marginal"),
       tr("Finds the curvature from the top and bottom edges of the page against a dark background. Experimental.")},
      {MANUAL, tr("Manual"), tr("You place the curves yourself on the Dewarping tab.")},
  };

  QStringList toolTip;
  for (const Method& method : methods) {
    box->addItem(method.name, static_cast<int>(method.mode));
    toolTip.push_back(method.name + QStringLiteral(": ") + method.description);
  }
  // One tooltip for the whole list: tooltips of single entries only show while the list is open.
  box->setToolTip(toolTip.join(QLatin1Char('\n')));
  label->setToolTip(box->toolTip());
}

DewarpingMode DewarpingMethodBox::selectedMethod(const QComboBox* box) {
  return static_cast<DewarpingMode>(box->currentData().toInt());
}

void DewarpingMethodBox::selectMethod(QComboBox* box, const DewarpingMode method) {
  const int index = box->findData(static_cast<int>(method));
  if (index >= 0) {
    box->setCurrentIndex(index);
  }
}
}  // namespace output
