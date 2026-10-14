// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OptionsWidgetBinarizationSauvola.h"

#include <utility>

#include "../../Utils.h"


namespace output {

OptionsWidgetBinarizationSauvola::OptionsWidgetBinarizationSauvola(std::shared_ptr<Settings> settings)
    : m_settings(std::move(settings)),
      m_connectionManager(std::bind(&OptionsWidgetBinarizationSauvola::setupUiConnections, this)) {
  setupUi(this);
  // As the other fields in the step "Output": "999 px" and "-100".
  core::Utils::setNumberFieldWidth(windowSize, 999);
  for (QDoubleSpinBox* spinBox : {sauvolaDelta, sauvolaCoef}) {
    core::Utils::setNumberFieldWidth(spinBox, -100, 0);
  }

  m_delayedStateChanger.setSingleShot(true);

  setupUiConnections();
}

void OptionsWidgetBinarizationSauvola::updateUi(const PageId& pageId) {
  auto block = m_connectionManager.getScopedBlock();

  const Params params(m_settings->getParams(pageId));
  m_pageId = pageId;
  m_colorParams = params.colorParams();
  m_outputProcessingParams = m_settings->getOutputProcessingParams(pageId);

  updateView();
}

void OptionsWidgetBinarizationSauvola::sauvolaDeltaChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setThresholdAdjustment(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationSauvola::windowSizeChanged(int value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setWindowSize(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationSauvola::sauvolaCoefChanged(double value) {
  m_colorParams = m_settings->getParams(m_pageId).colorParams();
  BlackWhiteOptions opt(m_colorParams.blackWhiteOptions());
  opt.setSauvolaCoef(value);
  m_colorParams.setBlackWhiteOptions(opt);
  m_settings->setColorParams(m_pageId, m_colorParams);
  emit colorParamsChanged();

  m_delayedStateChanger.start(750);
}

void OptionsWidgetBinarizationSauvola::updateView() {
  BlackWhiteOptions blackWhiteOptions = m_colorParams.blackWhiteOptions();
  sauvolaDelta->setValue(blackWhiteOptions.thresholdAdjustment());
  windowSize->setValue(blackWhiteOptions.getWindowSize());
  sauvolaCoef->setValue(blackWhiteOptions.getSauvolaCoef());
}

void OptionsWidgetBinarizationSauvola::sendStateChanged() {
  emit stateChanged();
}

#define CONNECT(...) m_connectionManager.addConnection(connect(__VA_ARGS__))

void OptionsWidgetBinarizationSauvola::setupUiConnections() {
  CONNECT(sauvolaDelta, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationSauvola::sauvolaDeltaChanged);
  CONNECT(windowSize, &QSpinBox::valueChanged, this, &OptionsWidgetBinarizationSauvola::windowSizeChanged);
  CONNECT(sauvolaCoef, &QDoubleSpinBox::valueChanged, this, &OptionsWidgetBinarizationSauvola::sauvolaCoefChanged);
  CONNECT(&m_delayedStateChanger, &QTimer::timeout, this, &OptionsWidgetBinarizationSauvola::sendStateChanged);
}

#undef CONNECT
}  // namespace output
