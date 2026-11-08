// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidgetBinarizationLocal.h"

#include <utility>

#include "../../Utils.h"
#include "BinarizationMethods.h"


namespace output {

OptionsWidgetBinarizationLocal::OptionsWidgetBinarizationLocal(std::shared_ptr<Settings> settings,
                                                               const BinarizationMethod method)
    : m_settings(std::move(settings)),
      m_method(method),
      m_connectionManager(std::bind(&OptionsWidgetBinarizationLocal::setupUiConnections, this)) {
  setupUi(this);
  coef->setToolTip(usesWolfCoefficient(m_method) ? tr("Default value is 0.3.") : tr("Default value is 0.34."));
  // As the other fields in the step "Output": "999 px" and "-100".
  core::Utils::setNumberFieldWidth(windowSize, 999);
  for (QDoubleSpinBox* spinBox : {delta, coef}) {
    core::Utils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_delayedStateChanger.setSingleShot(true);

  setupUiConnections();
}

void OptionsWidgetBinarizationLocal::updateUi(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  const Params params(m_settings->getParams(pageId));
  m_pageId = pageId;
  m_colorParams = params.colorParams();
  m_outputProcessingParams = m_settings->getOutputProcessingParams(pageId);

  updateView();
}

void OptionsWidgetBinarizationLocal::deltaChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setThresholdAdjustment(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationLocal::windowSizeChanged(int value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setWindowSize(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationLocal::coefChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  if (usesWolfCoefficient(m_method)) {
    opt.setWolfCoef(value);
  } else {
    opt.setSauvolaCoef(value);
  }
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationLocal::updateView() {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  delta->setValue(blackWhiteOptions.thresholdAdjustment());
  windowSize->setValue(blackWhiteOptions.getWindowSize());
  coef->setValue(usesWolfCoefficient(m_method) ? blackWhiteOptions.getWolfCoef() : blackWhiteOptions.getSauvolaCoef());
}

void OptionsWidgetBinarizationLocal::sendStateChanged() {
  emit stateChanged();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidgetBinarizationLocal::setupUiConnections() {
  CONNECT(delta, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationLocal::deltaChanged);
  CONNECT(windowSize, &QSpinBox::valueChanged, this, &OptionsWidgetBinarizationLocal::windowSizeChanged);
  CONNECT(coef, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationLocal::coefChanged);
  CONNECT(&m_delayedStateChanger, &QTimer::timeout, this, &OptionsWidgetBinarizationLocal::sendStateChanged);
}

#undef CONNECT
}  // namespace output
