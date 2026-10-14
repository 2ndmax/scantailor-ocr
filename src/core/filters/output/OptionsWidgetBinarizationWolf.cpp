// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidgetBinarizationWolf.h"

#include <utility>

#include "../../Utils.h"

namespace output {

OptionsWidgetBinarizationWolf::OptionsWidgetBinarizationWolf(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)),
      m_connectionManager(std::bind(&OptionsWidgetBinarizationWolf::setupUiConnections, this)) {
  setupUi(this);
  // As the other fields in the step "Output": "999 px" and "-100".
  core::Utils::setNumberFieldWidth(windowSize, 999);
  for (QAbstractSpinBox* spinBox : std::initializer_list<QAbstractSpinBox*>{wolfDelta, wolfCoef}) {
    core::Utils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_delayedStateChanger.setSingleShot(true);

  setupUiConnections();
}

void OptionsWidgetBinarizationWolf::updateUi(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  const Params params(m_settings->getParams(pageId));
  m_pageId = pageId;
  m_colorParams = params.colorParams();
  m_outputProcessingParams = m_settings->getOutputProcessingParams(pageId);

  updateView();
}

void OptionsWidgetBinarizationWolf::wolfDeltaChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setThresholdAdjustment(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationWolf::windowSizeChanged(int value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setWindowSize(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationWolf::wolfCoefChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setWolfCoef(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationWolf::updateView() {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  wolfDelta->setValue(blackWhiteOptions.thresholdAdjustment());
  windowSize->setValue(blackWhiteOptions.getWindowSize());
  wolfCoef->setValue(blackWhiteOptions.getWolfCoef());
}

void OptionsWidgetBinarizationWolf::sendStateChanged() {
  emit stateChanged();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidgetBinarizationWolf::setupUiConnections() {
  CONNECT(wolfDelta, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationWolf::wolfDeltaChanged);
  CONNECT(windowSize, &QSpinBox::valueChanged, this, &OptionsWidgetBinarizationWolf::windowSizeChanged);
  CONNECT(wolfCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationWolf::wolfCoefChanged);
  CONNECT(&m_delayedStateChanger, &QTimer::timeout, this, &OptionsWidgetBinarizationWolf::sendStateChanged);
}

#undef CONNECT
}  // namespace output
