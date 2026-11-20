// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_PAGELAYOUTPARAMS_H_
#define SCANTAILOR_PAGE_LAYOUT_PAGELAYOUTPARAMS_H_

#include <Margins.h>

#include <QDomDocument>
#include <QDomElement>

#include "Alignment.h"
#include "OutputSizeOptions.h"

namespace page_layout {
class PageLayoutParams {
 public:
  PageLayoutParams();

  PageLayoutParams(const Margins& hardMargins, const Alignment& alignment, bool autoMargins);

  explicit PageLayoutParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  const Margins& getHardMargins() const;

  void setHardMargins(const Margins& hardMargins);

  const Alignment& getAlignment() const;

  void setAlignment(const Alignment& alignment);

  bool isAutoMargins() const;

  void setAutoMargins(bool autoMargins);

  const OutputSizeOptions& getOutputSizeOptions() const;

  void setOutputSizeOptions(const OutputSizeOptions& options);

 private:
  Margins m_hardMargins;
  Alignment m_alignment;
  bool m_autoMargins;
  OutputSizeOptions m_outputSizeOptions;
};
}  // namespace page_layout

#endif  // ifndef SCANTAILOR_PAGE_LAYOUT_PAGELAYOUTPARAMS_H_