// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_SELECT_CONTENT_SELECTCONTENTPARAMS_H_
#define SCANTAILOR_SELECT_CONTENT_SELECTCONTENTPARAMS_H_

#include <AutoManualMode.h>

#include <QDomDocument>
#include <QDomElement>
#include <QSizeF>

#include "DetectionOptions.h"

namespace select_content {
class SelectContentParams {
 public:
  SelectContentParams();

  SelectContentParams(const QSizeF& pageRectSize,
                      bool contentDetectEnabled,
                      AutoManualMode pageDetectMode,
                      bool fineTuneCorners);

  explicit SelectContentParams(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  const QSizeF& getPageRectSize() const;

  void setPageRectSize(const QSizeF& pageRectSize);

  bool isContentDetectEnabled() const;

  void setContentDetectEnabled(bool contentDetectEnabled);

  bool isFineTuneCorners() const;

  void setFineTuneCorners(bool fineTuneCorners);

  AutoManualMode getPageDetectMode() const;

  void setPageDetectMode(AutoManualMode pageDetectMode);

  const DetectionOptions& getDetectionOptions() const;

  void setDetectionOptions(const DetectionOptions& options);

 private:
  QSizeF m_pageRectSize;
  bool m_contentDetectEnabled;
  AutoManualMode m_pageDetectMode;
  bool m_fineTuneCorners;
  DetectionOptions m_detectionOptions;
};
}  // namespace select_content

#endif  // ifndef SCANTAILOR_SELECT_CONTENT_SELECTCONTENTPARAMS_H_