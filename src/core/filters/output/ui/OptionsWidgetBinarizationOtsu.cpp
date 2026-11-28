// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidgetBinarizationOtsu.h"

#include <utility>

#include "../ColorParams.h"
#include "../Settings.h"
#include "WidgetUtils.h"

namespace output {

OptionsWidgetBinarizationOtsu::OptionsWidgetBinarizationOtsu(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)),
      m_connectionManager(std::bind(&OptionsWidgetBinarizationOtsu::setupUiConnections, this)) {
  setupUi(this);
  // As the other fields in the step "Output".
  core::WidgetUtils::setNumberFieldWidth(otsuDelta, -100, 0);

  m_delayedStateChanger.setSingleShot(true);

  setupUiConnections();
}

void OptionsWidgetBinarizationOtsu::updateUi(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();
  m_delayedStateChanger.stop();

  const Params params(m_settings->getParams(pageId));
  m_pageId = pageId;

  updateView(params.colorParams());
}

void OptionsWidgetBinarizationOtsu::otsuDeltaChanged(double value) {
  ColorParams colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(colorParams.blackWhiteOptions());
  BlackWhiteOptions::MethodValues values = opt.values(T_OTSU);
  values.thresholdAdjustment = static_cast<int>(value);
  opt.setValues(T_OTSU, values);
  colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationOtsu::updateView(const ColorParams& colorParams) {
  otsuDelta->setValue(colorParams.blackWhiteOptions().values(T_OTSU).thresholdAdjustment);
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidgetBinarizationOtsu::setupUiConnections() {
  CONNECT(otsuDelta, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationOtsu::otsuDeltaChanged);
  CONNECT(&m_delayedStateChanger, &QTimer::timeout, this, &OptionsWidgetBinarizationOtsu::sendStateChanged);
}

#undef CONNECT

void OptionsWidgetBinarizationOtsu::sendStateChanged() {
  emit stateChanged();
}
}  // namespace output
