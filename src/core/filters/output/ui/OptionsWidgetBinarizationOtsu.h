// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OTSUBINARIZATIONOPTIONSWIDGET_H_
#define SCANTAILOR_OUTPUT_OTSUBINARIZATIONOPTIONSWIDGET_H_

#include <ConnectionManager.h>

#include <QTimer>
#include <memory>

#include "OptionsWidgetBinarization.h"
#include "ui_OptionsWidgetBinarizationOtsu.h"

namespace output {
class ColorParams;
class Settings;

class OptionsWidgetBinarizationOtsu : public OptionsWidgetBinarization, private Ui::OptionsWidgetBinarizationOtsu {
  Q_OBJECT
 public:
  explicit OptionsWidgetBinarizationOtsu(std::shared_ptr<Settings> settings);

  ~OptionsWidgetBinarizationOtsu() override = default;

  BinarizationMethod method() const override { return T_OTSU; }

  void updateUi(const PageId& pageId) override;

 private slots:

  void otsuDeltaChanged(double value);

  void sendStateChanged();

 private:
  void updateView(const ColorParams& colorParams);

  void setupUiConnections();

  std::shared_ptr<Settings> m_settings;
  PageId m_pageId;
  QTimer m_delayedStateChanger;

  ConnectionManager m_connectionManager;
};
}  // namespace output


#endif  // SCANTAILOR_OUTPUT_OTSUBINARIZATIONOPTIONSWIDGET_H_
