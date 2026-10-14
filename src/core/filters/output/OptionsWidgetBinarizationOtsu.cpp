// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidgetBinarizationOtsu.h"

#include <utility>

#include "../../Utils.h"

namespace output {

OptionsWidgetBinarizationOtsu::OptionsWidgetBinarizationOtsu(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)),
      m_connectionManager(std::bind(&OptionsWidgetBinarizationOtsu::setupUiConnections, this)) {
  setupUi(this);
  // As the other fields in the step "Output".
  core::Utils::setNumberFieldWidth(otsuDelta, -100, 0);

  m_delayedStateChanger.setSingleShot(true);

  setupUiConnections();
}

void OptionsWidgetBinarizationOtsu::updateUi(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  const Params params(m_settings->getParams(pageId));
  m_pageId = pageId;
  m_colorParams = params.colorParams();

  updateView();
}

void OptionsWidgetBinarizationOtsu::otsuDeltaChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setThresholdAdjustment(static_cast<int>(value));
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationOtsu::updateView() {
  otsuDelta->setValue(m_colorParams.blackWhiteOptions().thresholdAdjustment());
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
