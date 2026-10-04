// Copyright (C) 2020  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ZoneEditorBase.h"

#include <QKeyEvent>
#include <QShortcut>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QStatusBar>

#include "ApplicationSettings.h"
#include "Utils.h"
#include "ZoneModeListener.h"

class ZoneEditorBase::ZoneModeProvider {
  DECLARE_NON_COPYABLE(ZoneModeProvider)
 public:
  explicit ZoneModeProvider(ZoneEditorBase& parent);

  ~ZoneModeProvider();

  void addListener(ZoneModeListener* listener);

  void removeAllListeners();

  void updateZoneMode();

 private:
  void notifyProviderStopped() const;

  std::list<ZoneModeListener*> m_listeners;
  ZoneEditorBase& m_parent;
};

// The stored values (0 = polygonal, 1 = lasso, 2 = rectangular) differ from the enum order.
static ZoneCreationMode zoneCreationModeFromInt(int v) {
  switch (v) {
    case 1:
      return ZoneCreationMode::LASSO;
    case 2:
      return ZoneCreationMode::RECTANGULAR;
    default:
      return ZoneCreationMode::POLYGONAL;
  }
}

static int zoneCreationModeToInt(ZoneCreationMode mode) {
  switch (mode) {
    case ZoneCreationMode::LASSO:
      return 1;
    case ZoneCreationMode::RECTANGULAR:
      return 2;
    default:
      return 0;
  }
}

ZoneEditorBase::ZoneEditorBase(const QImage& image,
                               const ImagePixmapUnion& downscaledVersion,
                               const ImagePresentation& presentation,
                               const Margins& margins)
    : ImageViewBase(image, downscaledVersion, presentation, margins),
      m_context(*this, m_zones),
      m_zoneModeProvider(std::make_unique<ZoneModeProvider>(*this)) {
  m_context.setZoneCreationMode(
      zoneCreationModeFromInt(ApplicationSettings::getInstance().getDefaultZoneCreationMode()));  // Issue #15.
  m_shortcutPolygonal = new QShortcut(Qt::Key_Z, this);
  m_shortcutPolygonal->setAutoRepeat(false);
  m_shortcutLasso = new QShortcut(Qt::Key_X, this);
  m_shortcutLasso->setAutoRepeat(false);
  m_shortcutRectangular = new QShortcut(Qt::Key_C, this);
  m_shortcutRectangular->setAutoRepeat(false);
  connect(m_shortcutPolygonal, &QShortcut::activated, [this]() { setZoneCreationMode(ZoneCreationMode::POLYGONAL); });
  connect(m_shortcutLasso, &QShortcut::activated, [this]() { setZoneCreationMode(ZoneCreationMode::LASSO); });
  connect(m_shortcutRectangular, &QShortcut::activated,
          [this]() { setZoneCreationMode(ZoneCreationMode::RECTANGULAR); });
}

ZoneEditorBase::~ZoneEditorBase() = default;

void ZoneEditorBase::setZoneCreationMode(const ZoneCreationMode mode) {
  m_context.setZoneCreationMode(mode);
  ApplicationSettings::getInstance().setDefaultZoneCreationMode(zoneCreationModeToInt(mode));
  m_zoneModeProvider->updateZoneMode();
}

void ZoneEditorBase::showEvent(QShowEvent* event) {
  ImageViewBase::showEvent(event);
  if (auto* mainWindow = dynamic_cast<QMainWindow*>(window())) {
    if (auto* zoneModeListener = core::Utils::castOrFindChild<ZoneModeListener*>(mainWindow->statusBar())) {
      m_zoneModeProvider->addListener(zoneModeListener);
    }
  }
}

void ZoneEditorBase::hideEvent(QHideEvent* event) {
  m_zoneModeProvider->removeAllListeners();
  ImageViewBase::hideEvent(event);
}

ZoneEditorBase::ZoneModeProvider::ZoneModeProvider(ZoneEditorBase& parent) : m_parent(parent) {}

ZoneEditorBase::ZoneModeProvider::~ZoneModeProvider() {
  notifyProviderStopped();
}

void ZoneEditorBase::ZoneModeProvider::addListener(ZoneModeListener* listener) {
  // The listener drops this callback in onZoneModeProviderStopped(), which we call
  // on hiding and on destruction, so it never outlives m_parent.
  ZoneEditorBase* const parent = &m_parent;
  listener->onZoneModeProviderStarted([parent](ZoneCreationMode mode) { parent->setZoneCreationMode(mode); });
  listener->onZoneModeChanged(m_parent.context().getZoneCreationMode());
  m_listeners.push_back(listener);
}

void ZoneEditorBase::ZoneModeProvider::removeAllListeners() {
  notifyProviderStopped();
  m_listeners.clear();
}

void ZoneEditorBase::ZoneModeProvider::updateZoneMode() {
  ZoneCreationMode mode = m_parent.context().getZoneCreationMode();
  for (ZoneModeListener* listener : m_listeners) {
    listener->onZoneModeChanged(mode);
  }
}

void ZoneEditorBase::ZoneModeProvider::notifyProviderStopped() const {
  for (ZoneModeListener* listener : m_listeners) {
    listener->onZoneModeProviderStopped();
  }
}
