// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_LOCALBINARIZATIONOPTIONSWIDGET_H_
#define SCANTAILOR_OUTPUT_LOCALBINARIZATIONOPTIONSWIDGET_H_

#include <core/ConnectionManager.h>

#include <QtCore>
#include <list>
#include <memory>

#include "BlackWhiteOptions.h"
#include "ColorParams.h"
#include "OptionsWidgetBinarization.h"
#include "Settings.h"
#include "ui_OptionsWidgetBinarizationLocal.h"

namespace output {
// The settings of a method with a local window: all methods except Otsu.
class OptionsWidgetBinarizationLocal : public OptionsWidgetBinarization, private Ui::OptionsWidgetBinarizationLocal {
  Q_OBJECT
 public:
  OptionsWidgetBinarizationLocal(std::shared_ptr<Settings> settings, BinarizationMethod method);

  ~OptionsWidgetBinarizationLocal() override = default;

  void updateUi(const PageId& m_pageId) override;

 private slots:

  void deltaChanged(double value);

  void windowSizeChanged(int value);

  void coefChanged(double value);

  void sendStateChanged();

 private:
  void updateView();

  void setupUiConnections();

  std::shared_ptr<Settings> m_settings;
  BinarizationMethod m_method;
  PageId m_pageId;
  ColorParams m_colorParams;
  QTimer m_delayedStateChanger;
  OutputProcessingParams m_outputProcessingParams;

  ConnectionManager m_connectionManager;
};
}  // namespace output


#endif  // SCANTAILOR_OUTPUT_LOCALBINARIZATIONOPTIONSWIDGET_H_
