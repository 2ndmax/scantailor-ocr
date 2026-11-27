// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_ux_qt.h"

#include <Application.h>
#include <ApplicationSettings.h>
#include <AutoRemovingFile.h>
#include <BasicImageView.h>
#include <BubbleAnimation.h>
#include <CollapsibleGroupBox.h>
#include <ColorSchemeManager.h>
#include <DebugImageView.h>
#include <Dpi.h>
#include <DraggableLineSegment.h>
#include <DraggablePoint.h>
#include <DraggablePolygon.h>
#include <EditableSpline.h>
#include <EditableZoneSet.h>
#include <ErrorWidget.h>
#include <FocusFrameStyle.h>
#include <ImageId.h>
#include <ImageTransformation.h>
#include <InteractionHandler.h>
#include <InteractionState.h>
#include <InteractiveXSpline.h>
#include <NativeColorScheme.h>
#include <ObjectDragHandler.h>
#include <OverlayStyle.h>
#include <PageSelectionAccessor.h>
#include <PageSequence.h>
#include <ProcessingIndicationWidget.h>
#include <SerializableSpline.h>
#include <StageListView.h>
#include <StyleSheetColorScheme.h>
#include <StyledIconPack.h>
#include <ThumbnailBase.h>
#include <ThumbnailPixmapCache.h>
#include <XSpline.h>
#include <Zone.h>
#include <ZoneContextMenuInteraction.h>
#include <ZoneDefaultInteraction.h>
#include <ZoneDragInteraction.h>
#include <ZoneInteractionContext.h>
#include <ZoneVertexDragInteraction.h>
#include <ZoomHandler.h>
#include <color_schemes/dark_scheme_colors.h>
#include <color_schemes/light_scheme_colors.h>
#include <core/filters/output/ui/TabbedImageView.h>

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointingDevice>
#include <QPushButton>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QSignalSpy>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleOptionFocusRect>
#include <QTemporaryDir>
#include <QTimerEvent>
#include <QWheelEvent>
#include <QtTest/QtTest>
#include <algorithm>
#include <cmath>
#include <memory>

void TestUxQt::testCollapsibleGroupBoxStateAndSignal() {
  CollapsibleGroupBox group("Options");
  QSignalSpy spy(&group, &CollapsibleGroupBox::collapsedStateChanged);
  QVERIFY(spy.isValid());

  QVERIFY(!group.isCollapsed());

  group.setCollapsed(true);
  QVERIFY(group.isCollapsed());
  QCOMPARE(spy.count(), 1);
  QCOMPARE(spy.at(0).at(0).toBool(), true);

  group.setCollapsed(false);
  QVERIFY(!group.isCollapsed());
  QCOMPARE(spy.count(), 2);
  QCOMPARE(spy.at(1).at(0).toBool(), false);
}

void TestUxQt::testCollapsedStateIsLoadedForGroupDisabledAtStart() {
  // A group that is disabled when it is first shown (e.g. the picture options on a black and white page)
  // must still get the state the user left it in, or the next start overwrites that state.
  const QString key = "CollapsibleGroupBox/disabledAtStart";
  QSettings().setValue(key + "/collapsed", true);
  {
    CollapsibleGroupBox group("Options");
    group.setObjectName("disabledAtStart");
    group.setEnabled(false);
    group.show();
    QVERIFY(group.isCollapsed());
  }
  QSettings().remove(key);
}

void TestUxQt::testRemovedChildIsForgottenByCollapsedGroup() {
  // A child the group hid when collapsing and that is removed then must not be shown by a later
  // expanding: here it comes back hidden on purpose, as an object at the same address would.
  {
    CollapsibleGroupBox group("Options");
    group.setObjectName("removedChild");
    auto* label = new QLabel("text", &group);
    group.show();
    group.setCollapsed(true);
    QVERIFY(label->isHidden());

    label->setParent(nullptr);
    group.setCollapsed(false);
    label->setParent(&group);
    label->hide();
    group.setCollapsed(true);
    group.setCollapsed(false);
    QVERIFY(label->isHidden());
  }
  QSettings().remove("CollapsibleGroupBox/removedChild");
  QVERIFY(!QSettings().contains("CollapsibleGroupBox/removedChild/collapsed"));
}

void TestUxQt::testErrorWidgetShowsItsText() {
  // The text and its format reach the label of the widget.
  const QString text("<b>Test error</b>");
  ErrorWidget widget(text, Qt::RichText);
  const QList<QLabel*> labels = widget.findChildren<QLabel*>();
  const auto textLabel
      = std::find_if(labels.begin(), labels.end(), [&](const QLabel* label) { return label->text() == text; });
  QVERIFY(textLabel != labels.end());
  QCOMPARE((*textLabel)->textFormat(), Qt::RichText);
}

void TestUxQt::testStyledIconFollowsPaletteColor() {
  // Styled icons are colored with the palette's text color and kept for the next repaint; a new
  // color scheme must still recolor them.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  QFile svg(dir.filePath("square.svg"));
  QVERIFY(svg.open(QIODevice::WriteOnly));
  svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\"/></svg>");
  svg.close();
  QFile sheet(dir.filePath("icons.xml"));
  QVERIFY(sheet.open(QIODevice::WriteOnly));
  sheet.write(
      ("<icons><icon name=\"square\" styled=\"on\"><file>" + svg.fileName() + "</file></icon></icons>").toUtf8());
  sheet.close();

  StyledIconPack pack(sheet.fileName());
  const QIcon icon = pack.getIcon("square");
  const auto centerColor = [&]() { return icon.pixmap(QSize(16, 16)).toImage().pixelColor(8, 8); };

  const QPalette oldPalette = QGuiApplication::palette();
  QPalette palette(oldPalette);
  palette.setColor(QPalette::Normal, QPalette::WindowText, QColor(0x20, 0x80, 0x40));
  QGuiApplication::setPalette(palette);
  const QColor first = centerColor();
  palette.setColor(QPalette::Normal, QPalette::WindowText, QColor(0xa0, 0x10, 0x10));
  QGuiApplication::setPalette(palette);
  const QColor second = centerColor();
  QGuiApplication::setPalette(oldPalette);

  QCOMPARE(first, QColor(0x20, 0x80, 0x40));
  QCOMPARE(second, QColor(0xa0, 0x10, 0x10));
}

namespace {
/** An image that is a thumbnail in \p cache, once its background loading has finished. */
bool addLoadedThumbnail(ThumbnailPixmapCache& cache, const ImageId& id, const QImage& image) {
  if (!image.save(id.filePath())) {
    return false;
  }
  cache.ensureThumbnailExists(id, image);
  QPixmap loaded;
  const bool loadedInTime
      = QTest::qWaitFor([&] { return cache.loadRequest(id, loaded, {}) == ThumbnailPixmapCache::LOADED; }, 10000);
  return loadedInTime && !loaded.isNull();
}

QImage paintThumbnail(ThumbnailBase& thumb) {
  const QRectF bounds = thumb.boundingRect();
  QImage target(int(std::ceil(bounds.width())) + 4, int(std::ceil(bounds.height())) + 4,
                QImage::Format_ARGB32_Premultiplied);
  target.fill(Qt::white);
  QPainter painter(&target);
  thumb.paint(&painter, nullptr, nullptr);
  painter.end();
  return target;
}
}  // namespace

void TestUxQt::testThumbnailDoesNotKeepThePreviousOne() {
  // All thumbnails are drawn through one temporary pixmap; what one leaves there must not show
  // in the next, e.g. at the antialiased edges of a deskewed page.
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  auto cache = std::make_shared<ThumbnailPixmapCache>(dir.path(), QSize(250, 250), 100, 5);
  QImage dark(2000, 3000, QImage::Format_RGB32);
  dark.fill(qRgb(0x10, 0x20, 0x30));
  QImage light(2000, 3000, QImage::Format_RGB32);
  light.fill(qRgb(0xf0, 0xe0, 0xd0));
  const ImageId darkId(dir.filePath("dark.png"));
  const ImageId lightId(dir.filePath("light.png"));
  QVERIFY(addLoadedThumbnail(*cache, darkId, dark));
  QVERIFY(addLoadedThumbnail(*cache, lightId, light));

  ImageTransformation xform(QRectF(0, 0, 2000, 3000), Dpi(300, 300));
  xform.setPostRotation(3.0);
  ThumbnailBase darkThumb(cache, QSizeF(250, 250), darkId, xform);
  ThumbnailBase lightThumb(cache, QSizeF(250, 250), lightId, xform);

  paintThumbnail(lightThumb);
  const QImage lightAlone = paintThumbnail(lightThumb);
  paintThumbnail(darkThumb);
  QCOMPARE(paintThumbnail(lightThumb), lightAlone);
}

void TestUxQt::testThumbnailBorderIsTheAccentColor() {
  // The selected thumbnail and the marks take the accent color of the scheme, or the chosen one.
  const auto border
      = [](const ColorScheme& scheme) { return scheme.getColorParams()->at("ThumbnailSequenceSelectedItemBorder"); };
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  settings.setAccentColor(QColor());
  QCOMPARE(border(*StyleSheetColorScheme::of<light_scheme_colors>()), QColor(light_scheme_colors::accent));
  QCOMPARE(border(*StyleSheetColorScheme::of<dark_scheme_colors>()), QColor(dark_scheme_colors::accent));

  const QColor chosen(0x12, 0x9a, 0x45);
  settings.setAccentColor(chosen);
  const QColor lightBorder = border(*StyleSheetColorScheme::of<light_scheme_colors>());
  const QColor darkBorder = border(*StyleSheetColorScheme::of<dark_scheme_colors>());
  settings.setAccentColor(QColor());
  QCOMPARE(lightBorder, chosen);
  QCOMPARE(darkBorder, chosen);
}

void TestUxQt::testColorSchemesSetTheSameParameters() {
  // The call sites have no fallback colors: every scheme sets every parameter.
  const auto names = [](const ColorScheme& scheme) {
    QStringList list;
    for (const auto& [name, color] : *scheme.getColorParams()) {
      list << name;
    }
    list.sort();
    return list;
  };
  const QStringList light = names(*StyleSheetColorScheme::of<light_scheme_colors>());
  QVERIFY(!light.isEmpty());
  QCOMPARE(names(*StyleSheetColorScheme::of<dark_scheme_colors>()), light);
  QCOMPARE(names(NativeColorScheme()), light);
}

void TestUxQt::testTextOnAccentHasTheHigherContrast() {
  const QColor light("#ffffff");
  const QColor dark("#262626");
  // The default accent colors of Light and Dark, and chosen ones of either lightness.
  QCOMPARE(StyleSheetColorScheme::textOnAccent(QColor(light_scheme_colors::accent), light, dark), light);
  QCOMPARE(StyleSheetColorScheme::textOnAccent(QColor(dark_scheme_colors::accent), light, dark), dark);
  QCOMPARE(StyleSheetColorScheme::textOnAccent(QColor("#000080"), light, dark), light);
  QCOMPARE(StyleSheetColorScheme::textOnAccent(QColor("#ffff00"), light, dark), dark);

  // A light accent color chosen in the Light scheme gets dark text, in the palette and on thumbnails.
  ApplicationSettings& settings = ApplicationSettings::getInstance();
  settings.setAccentColor(QColor("#ffff00"));
  const auto scheme = StyleSheetColorScheme::of<light_scheme_colors>();
  settings.setAccentColor(QColor());
  const QColor expected(light_scheme_colors::darkTextOnAccent);
  QCOMPARE(scheme->getPalette()->color(QPalette::HighlightedText), expected);
  QCOMPARE(scheme->getColorParams()->at("ThumbnailSequenceSelectionLeaderText"), expected);
}

void TestUxQt::testNativeSchemeFollowsTheSameMeanings() {
  // The native scheme has the system's palette and takes its color parameters from it by the same
  // meanings as Light and Dark: the selection color as the accent, its text color as text on it.
  // The palette has no error color, so it is the one of Light or Dark, whichever matches the window.
  const QPalette palette;
  const NativeColorScheme scheme;
  const ColorScheme::ColorParams& params = *scheme.getColorParams();
  QCOMPARE(params.at("ThumbnailSequenceItemText"), palette.color(QPalette::Text));
  QCOMPARE(params.at("ThumbnailSequenceSelectedItemBorder"), palette.color(QPalette::Highlight));
  QCOMPARE(params.at("ThumbnailSequenceSelectionLeaderBackground"), palette.color(QPalette::Highlight));
  QCOMPARE(params.at("ThumbnailSequenceSelectionLeaderText"), palette.color(QPalette::HighlightedText));
  QCOMPARE(params.at("OpenNewProjectBorder"), palette.color(QPalette::Mid));
  QCOMPARE(params.at("ProcessingIndicationHead"), palette.color(QPalette::Text));
  QCOMPARE(params.at("ProcessingIndicationTail"), palette.color(QPalette::Highlight));
  QCOMPARE(params.at("StageListHead"), palette.color(QPalette::Text));
  QCOMPARE(params.at("StageListTail"), palette.color(QPalette::Highlight));
  const bool darkWindow = palette.color(QPalette::Window).lightnessF() < 0.5;
  QCOMPARE(params.at("ErrorText"), QColor(darkWindow ? dark_scheme_colors::error : light_scheme_colors::error));
}

void TestUxQt::testFocusFrameOnlyForKeyboardFocus() {
  // The focus frame of Light and Dark is dashed in the accent color, inside the border of a button and
  // around the content of other widgets; a focus from a mouse click shows none.
  const FocusFrameStyle style(QStyleFactory::create("Fusion"));
  QPalette palette;
  palette.setColor(QPalette::Highlight, Qt::red);
  palette.setColor(QPalette::Button, Qt::blue);
  QPushButton button;
  QCheckBox checkBox;
  button.resize(40, 20);
  checkBox.resize(40, 20);

  const auto drawFocusFrame = [&](const QWidget& widget, const QStyle::State state) {
    QStyleOptionFocusRect option;
    option.palette = palette;
    option.state = QStyle::State_Enabled | QStyle::State_HasFocus | state;
    option.rect = QRect(5, 5, 30, 10);  // the content, as the widgets give it
    QImage image(40, 20, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    style.drawPrimitive(QStyle::PE_FrameFocusRect, &option, &painter, &widget);
    painter.end();
    return image;
  };
  const auto countInRow = [](const QImage& image, const int y, const QColor& color) {
    int count = 0;
    for (int x = 0; x < image.width(); ++x) {
      count += (image.pixelColor(x, y) == color) ? 1 : 0;
    }
    return count;
  };

  QImage blank(40, 20, QImage::Format_RGB32);
  blank.fill(Qt::white);
  QCOMPARE(drawFocusFrame(button, QStyle::State_None), blank);
  QCOMPARE(drawFocusFrame(checkBox, QStyle::State_None), blank);

  // Two pixels inside the button's border, which stays untouched for the button's own marks.
  const QImage buttonFrame = drawFocusFrame(button, QStyle::State_KeyboardFocusChange);
  QCOMPARE(countInRow(buttonFrame, 0, Qt::white), 40);
  QCOMPARE(countInRow(buttonFrame, 1, Qt::white), 40);
  QVERIFY(countInRow(buttonFrame, 2, Qt::red) > 0);

  // Around the content of the check box, with gaps that leave the background.
  const QImage checkBoxFrame = drawFocusFrame(checkBox, QStyle::State_KeyboardFocusChange);
  QCOMPARE(countInRow(checkBoxFrame, 0, Qt::white), 40);
  QVERIFY(countInRow(checkBoxFrame, 5, Qt::red) > 0);
  QCOMPARE(countInRow(checkBoxFrame, 5, Qt::blue), 0);
}

namespace {
struct PanelWithControls {
  QWidget panel;
  QComboBox* list = new QComboBox(&panel);
  QSpinBox* numberField = new QSpinBox(&panel);
  QDoubleSpinBox* decimalField = new QDoubleSpinBox(&panel);
  QSlider* slider = new QSlider(Qt::Horizontal, &panel);
  QScrollBar* scrollBar = new QScrollBar(Qt::Vertical, &panel);
  QPushButton* button = new QPushButton(&panel);
};

/** What the program's Application installs; qt_tests runs under a plain QApplication. */
class InstalledWheelFocusTracker {
 public:
  InstalledWheelFocusTracker() { qApp->installEventFilter(&m_tracker); }
  ~InstalledWheelFocusTracker() { qApp->removeEventFilter(&m_tracker); }

 private:
  WheelFocusTracker m_tracker;
};
}  // namespace

void TestUxQt::testWheelGoesPastControlsThatWereNotClicked() {
  PanelWithControls controls;
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), &controls.panel);
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.numberField), &controls.panel);
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.decimalField), &controls.panel);
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.slider), &controls.panel);

  // Inside a number field the line edit gets the wheel first.
  auto* lineEdit = controls.numberField->findChild<QLineEdit*>();
  QVERIFY(lineEdit != nullptr);
  QCOMPARE(Application::wheelReceiverInsteadOf(lineEdit), &controls.panel);

  // A scroll bar is for scrolling, and other widgets have nothing to change.
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.scrollBar), static_cast<QWidget*>(nullptr));
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.button), static_cast<QWidget*>(nullptr));
  QCOMPARE(Application::wheelReceiverInsteadOf(&controls.panel), static_cast<QWidget*>(nullptr));
}

namespace {
/** Clicks \p control like the user does; a list's popup that opens on the click is closed again. */
void clickControl(QWidget* control) {
  QTest::mouseClick(control, Qt::LeftButton);
  if (auto* list = qobject_cast<QComboBox*>(control)) {
    list->hidePopup();
  }
}
[[nodiscard]] bool activateWindowForTest(QWidget& window) {
  window.show();
  window.raise();
  window.activateWindow();
  return QTest::qWaitForWindowActive(&window, 2000);
}
}  // namespace

void TestUxQt::testWheelStaysWithAClickedControl() {
  const InstalledWheelFocusTracker tracker;
  PanelWithControls controls;
  QVERIFY(activateWindowForTest(controls.panel));

  for (QWidget* control : std::vector<QWidget*>{controls.list, controls.numberField, controls.slider}) {
    clickControl(control);
    QTRY_VERIFY(control->hasFocus());
    QCOMPARE(Application::wheelReceiverInsteadOf(control), static_cast<QWidget*>(nullptr));
  }
}

void TestUxQt::testWheelStaysWithAClickedControlAfterAnotherWindow() {
  const InstalledWheelFocusTracker tracker;
  PanelWithControls controls;
  QVERIFY(activateWindowForTest(controls.panel));
  // The list has the focus from the opening of the window already, so the click brings no new focus.
  QTRY_VERIFY(controls.list->hasFocus());
  clickControl(controls.list);
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), static_cast<QWidget*>(nullptr));

  auto sendFocusCycle = [&](const Qt::FocusReason reason) {
    QFocusEvent outEvent(QEvent::FocusOut, reason);
    QCoreApplication::sendEvent(controls.list, &outEvent);
    QFocusEvent inEvent(QEvent::FocusIn, reason);
    QCoreApplication::sendEvent(controls.list, &inEvent);
  };

  sendFocusCycle(Qt::ActiveWindowFocusReason);
  QVERIFY(controls.list->hasFocus());
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), static_cast<QWidget*>(nullptr));

  sendFocusCycle(Qt::PopupFocusReason);
  QVERIFY(controls.list->hasFocus());
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), static_cast<QWidget*>(nullptr));

  sendFocusCycle(Qt::TabFocusReason);
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), &controls.panel);
}

void TestUxQt::testWheelGoesPastAControlFocusedByOpeningAWindow() {
  const InstalledWheelFocusTracker tracker;
  // Like "Default Parameters": the dialog opens with the focus on its list of profiles.
  QDialog dialog;
  auto* list = new QComboBox(&dialog);
  new QSpinBox(&dialog);
  QVERIFY(activateWindowForTest(dialog));
  QTRY_VERIFY(list->hasFocus());
  QCOMPARE(Application::wheelReceiverInsteadOf(list), static_cast<QWidget*>(&dialog));
}

void TestUxQt::testWheelGoesPastAControlThatGotTheFocusOfAHiddenOne() {
  const InstalledWheelFocusTracker tracker;
  PanelWithControls controls;
  QVERIFY(activateWindowForTest(controls.panel));
  clickControl(controls.numberField);

  // Qt moves the focus on as if by the Tab key.
  controls.numberField->hide();
  QWidget* const next = QApplication::focusWidget();
  QCOMPARE(next, static_cast<QWidget*>(controls.decimalField));
  QCOMPARE(Application::wheelReceiverInsteadOf(next), &controls.panel);

  // A click counts only until the control loses the focus. A slider, because on macOS a list takes no focus
  // by a click (see the next test).
  clickControl(controls.slider);
  QTRY_VERIFY(controls.slider->hasFocus());
  controls.list->setFocus();
  controls.slider->setFocus();
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.slider), &controls.panel);
}

void TestUxQt::testWheelGoesPastAControlClickedWithoutTakingTheFocus() {
  const InstalledWheelFocusTracker tracker;
  PanelWithControls controls;
  // On macOS a list takes no focus by a click, as the CI run there shows; sliders and number fields do.
  // Qt::TabFocus does the same on every system.
  controls.list->setFocusPolicy(Qt::TabFocus);
  QVERIFY(activateWindowForTest(controls.panel));
  controls.numberField->setFocus();

  clickControl(controls.list);
  QVERIFY(!controls.list->hasFocus());

  // Later the list gets the focus without a click, e.g. by the Tab key.
  controls.list->setFocus();
  QCOMPARE(Application::wheelReceiverInsteadOf(controls.list), &controls.panel);
}

void TestUxQt::testImageViewBaseEmptyAndZeroWidthGuards() {
  BasicImageView emptyView(QImage{});
  emptyView.resize(0, 0);
  emptyView.show();
  QCoreApplication::processEvents();
  emptyView.setZoomLevel(2.0);
  emptyView.setWidgetFocalPointWithoutMoving(QPointF(10.0, 10.0));
  QVERIFY(emptyView.virtualDisplayRect().isEmpty());

  QImage img(40, 40, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  BasicImageView validView(img);
  validView.resize(200, 200);
  validView.setZoomLevel(2.0);
  QCOMPARE(validView.zoomLevel(), 2.0);
}

void TestUxQt::testBubbleAnimationDegenerateInputs() {
  BubbleAnimation zeroBubbles(0);
  QImage canvas(32, 32, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::transparent);
  QVERIFY(!zeroBubbles.nextFrame(Qt::red, Qt::blue, static_cast<QPaintDevice*>(nullptr)));
  QVERIFY(!zeroBubbles.nextFrame(Qt::red, Qt::blue, static_cast<QPainter*>(nullptr), QRectF(0, 0, 10, 10)));
  // numBubbles == 0 clamps to 1 bubble; a 1-frame cycle returns false (cycle complete) safely without UB.
  QVERIFY(!zeroBubbles.nextFrame(Qt::red, Qt::blue, &canvas));

  BubbleAnimation anim(8);
  QImage hiDpi(64, 64, QImage::Format_ARGB32_Premultiplied);
  hiDpi.fill(Qt::transparent);
  hiDpi.setDevicePixelRatio(2.0);
  QVERIFY(anim.nextFrame(Qt::red, Qt::blue, &hiDpi));
}

void TestUxQt::testWheelPositionMapping() {
  QWidget panel;
  panel.resize(200, 200);
  auto* spin = new QSpinBox(&panel);
  spin->setGeometry(40, 50, 80, 30);

  QWidget other;
  other.resize(100, 100);

  const QPointF childLocal(5.0, 6.0);
  const QPointF childGlobal = spin->mapToGlobal(childLocal);
  QWheelEvent childWheel(childLocal, childGlobal, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized,
                         QPointingDevice::primaryPointingDevice());
  QCOMPARE(Application::mapWheelPosition(spin, &panel, childWheel), spin->mapTo(&panel, childLocal));

  const QPointF otherGlobal(250.0, 260.0);
  QWheelEvent otherWheel(QPointF(10.0, 10.0), otherGlobal, QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                         Qt::NoScrollPhase, false, Qt::MouseEventNotSynthesized,
                         QPointingDevice::primaryPointingDevice());
  QCOMPARE(Application::mapWheelPosition(&other, &panel, otherWheel), QPointF(panel.mapFromGlobal(otherGlobal)));
}

void TestUxQt::testColorSchemeRoundedCornersAndTabbedView() {
  auto lightScheme = StyleSheetColorScheme::of<light_scheme_colors>();
  ColorSchemeManager::instance().setColorScheme(*lightScheme);
  QVERIFY(!ColorSchemeManager::instance().hasRoundedCorners());

  NativeColorScheme nativeScheme;
  ColorSchemeManager::instance().setColorScheme(nativeScheme);
  QCOMPARE(ColorSchemeManager::instance().hasRoundedCorners(), nativeScheme.hasRoundedCorners());

  output::TabbedImageView tabs;
  QCOMPARE(tabs.property("outputTabs").toBool(), true);
}

void TestUxQt::testDebugImageViewMissingFileShowsErrorWidget() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  DebugImageView view(AutoRemovingFile(dir.filePath("missing_debug_image.png")));
  view.setLive(true);
  QTRY_VERIFY_WITH_TIMEOUT(qobject_cast<ErrorWidget*>(view.currentWidget()) != nullptr, 5000);
  view.setLive(false);
}

void TestUxQt::testPageSelectionAccessorNullProvider() {
  PageSelectionAccessor accessor{std::shared_ptr<const PageSelectionProvider>()};
  QVERIFY(accessor.allPages().numPages() == 0);
  QVERIFY(accessor.selectedPages().empty());
  QVERIFY(accessor.selectedRanges().empty());
}

void TestUxQt::testStyledIconPackDegenerateAndHiDpiPixmap() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());

  QFile emptySheet(dir.filePath("empty.xml"));
  QVERIFY(emptySheet.open(QIODevice::WriteOnly));
  emptySheet.write("");
  emptySheet.close();
  StyledIconPack emptyPack(emptySheet.fileName());
  QVERIFY(emptyPack.getIcon("missing").pixmap(QSize(0, 0)).isNull());

  QFile svg(dir.filePath("square.svg"));
  QVERIFY(svg.open(QIODevice::WriteOnly));
  svg.write("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\"><rect width=\"10\" height=\"10\"/></svg>");
  svg.close();

  QFile sheet(dir.filePath("icons.xml"));
  QVERIFY(sheet.open(QIODevice::WriteOnly));
  sheet.write(
      ("<icons><icon name=\"square\" styled=\"on\"><file>" + svg.fileName() + "</file></icon></icons>").toUtf8());
  sheet.close();

  const QPalette oldPalette = QGuiApplication::palette();
  QPalette palette(oldPalette);
  palette.setColor(QPalette::Normal, QPalette::WindowText, QColor(0x20, 0x80, 0x40));
  QGuiApplication::setPalette(palette);

  StyledIconPack pack(sheet.fileName());
  const QPixmap pm = pack.getIcon("square").pixmap(QSize(16, 16), 2.0);
  QGuiApplication::setPalette(oldPalette);

  QCOMPARE(pm.size(), QSize(32, 32));
  QCOMPARE(pm.devicePixelRatio(), 2.0);
  QCOMPARE(pm.toImage().pixelColor(24, 24), QColor(0x20, 0x80, 0x40));
}

void TestUxQt::testOverlayStyleHiDpiHandlePixmap() {
  const QPixmap p1 = OverlayStyle::handlePixmap(1.0);
  const QPixmap p2 = OverlayStyle::handlePixmap(2.0);
  QCOMPARE(p1.size(), QSize(16, 16));
  QCOMPARE(p2.size(), QSize(32, 32));
  QCOMPARE(p2.devicePixelRatio(), 2.0);
}

void TestUxQt::testProcessingIndicationPartialRepaintAndHideStopsTimer() {
  ProcessingIndicationWidget widget;
  widget.resize(200, 200);
  widget.show();
  widget.processingRestartedEffect();

  QImage partial(200, 200, QImage::Format_ARGB32_Premultiplied);
  partial.fill(Qt::transparent);
  widget.render(&partial, QPoint(0, 0), QRegion(0, 0, 20, 20));
  QVERIFY(partial.pixelColor(5, 5).alpha() > 0);

  struct PaintCounter : public QObject {
    int count = 0;
    bool eventFilter(QObject*, QEvent* event) override {
      if (event->type() == QEvent::Paint) {
        ++count;
      }
      return false;
    }
  } counter;
  widget.installEventFilter(&counter);
  widget.hide();
  QTest::qWait(80);
  QCOMPARE(counter.count, 0);
}

void TestUxQt::testThumbnailEmptyAreaAndClipPreservation() {
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  auto cache = std::make_shared<ThumbnailPixmapCache>(dir.path(), QSize(250, 250), 100, 5);
  QImage img(100, 100, QImage::Format_RGB32);
  img.fill(Qt::blue);
  const ImageId id(dir.filePath("blue.png"));
  QVERIFY(addLoadedThumbnail(*cache, id, img));

  ImageTransformation xform(QRectF(0, 0, 100, 100), Dpi(300, 300));
  ThumbnailBase emptyThumb(cache, QSizeF(0, 0), id, xform);
  paintThumbnail(emptyThumb);

  ThumbnailBase thumb(cache, QSizeF(120, 120), id, xform);
  QImage canvas(160, 160, QImage::Format_ARGB32_Premultiplied);
  canvas.fill(Qt::white);
  QPainter painter(&canvas);
  const QRectF outerClip(10.0, 10.0, 40.0, 40.0);
  painter.setClipRect(outerClip);
  thumb.paint(&painter, nullptr, nullptr);
  QVERIFY(painter.hasClipping());
  QCOMPARE(painter.clipBoundingRect(), outerClip);
}

void TestUxQt::testStageListViewSafeWithoutStages() {
  StageListView view(nullptr);
  view.setBatchProcessingInProgress(true);
  view.setBatchProcessingPossible(true);
  view.resize(180, 180);
  view.setBatchProcessingInProgress(false);
  view.setPdfRowEnabled(false);
}

void TestUxQt::testZoomHandlerShortcutStack() {
  QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  BasicImageView view(img);

  const QList<QShortcut*> baseShortcuts = view.findChildren<QShortcut*>();
  QCOMPARE(baseShortcuts.size(), 2);
  QVERIFY(baseShortcuts[0]->isEnabled());
  QVERIFY(baseShortcuts[1]->isEnabled());

  {
    ZoomHandler topHandler(view);
    const QList<QShortcut*> allShortcuts = view.findChildren<QShortcut*>();
    QCOMPARE(allShortcuts.size(), 4);
    QVERIFY(!baseShortcuts[0]->isEnabled());
    QVERIFY(!baseShortcuts[1]->isEnabled());
  }

  QVERIFY(baseShortcuts[0]->isEnabled());
  QVERIFY(baseShortcuts[1]->isEnabled());
}

void TestUxQt::testInteractionHandlerSelfAndSiblingDeletionInDispatch() {
  struct ActionHandler : public InteractionHandler {
    std::function<void()> fn;
    int calls = 0;
    explicit ActionHandler(std::function<void()> callback) : fn(std::move(callback)) {}
    void onMousePressEvent(QMouseEvent*, InteractionState&) override {
      ++calls;
      if (fn) {
        fn();
      }
    }
  };

  InteractionHandler root;
  InteractionState state;
  std::unique_ptr<ActionHandler> victim(new ActionHandler({}));
  std::unique_ptr<ActionHandler> selfDel;
  ActionHandler afterVictim([&]() {});

  ActionHandler killer([&]() { victim.reset(); });
  selfDel = std::make_unique<ActionHandler>([&]() { selfDel.reset(); });

  root.makeLastFollower(killer);
  root.makeLastFollower(*victim);
  root.makeLastFollower(*selfDel);
  root.makeLastFollower(afterVictim);

  QMouseEvent press(QEvent::MouseButtonPress, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton, Qt::LeftButton,
                    Qt::NoModifier);
  press.ignore();
  root.mousePressEvent(&press, state);

  QCOMPARE(killer.calls, 1);
  QVERIFY(!victim);
  QVERIFY(!selfDel);
  QCOMPARE(afterVictim.calls, 1);
}

void TestUxQt::testInteractiveXSplineDegenerateAndDraggableDefaults() {
  QImage img(100, 100, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  BasicImageView view(img);
  InteractionState state;

  InteractiveXSpline splineHandler(view);
  splineHandler.proximityUpdate(QPointF(10, 10), state);
  splineHandler.removeControlPointUnderMouse();

  XSpline singlePointSpline;
  singlePointSpline.appendControlPoint(QPointF(20, 20), 0.0);
  splineHandler.setSpline(singlePointSpline);
  splineHandler.proximityUpdate(QPointF(20, 20), state);
  splineHandler.removeControlPointUnderMouse();

  DraggablePoint pt;
  pt.proximity(QPointF(0, 0));
  pt.dragInitiated(QPointF(0, 0));
  pt.dragContinuation(QPointF(10, 10), Qt::NoModifier);

  DraggableLineSegment seg;
  seg.proximity(QPointF(0, 0));
  seg.dragInitiated(QPointF(0, 0));
  seg.dragContinuation(QPointF(10, 10), Qt::NoModifier);

  DraggablePolygon poly;
  poly.proximity(QPointF(0, 0));
  poly.dragInitiated(QPointF(0, 0));
  poly.dragContinuation(QPointF(10, 10), Qt::NoModifier);

  ObjectDragHandler dragHandler(nullptr);
  dragHandler.forceEnterDragState(state, QPoint(5, 5));
}

void TestUxQt::testZoneInteractionsFixes() {
  QImage img(200, 200, QImage::Format_ARGB32_Premultiplied);
  img.fill(Qt::white);
  BasicImageView view(img);
  view.resize(400, 400);
  view.setZoomLevel(2.0);
  EditableZoneSet zones;
  ZoneInteractionContext ctx(view, zones);

  // P5-24: Empty spline in ZoneDragInteraction does not crash.
  {
    auto emptySpline = std::make_shared<EditableSpline>();
    InteractionState state;
    auto* emptyDrag = new ZoneDragInteraction(ctx, state, emptySpline, false);
    view.rootInteractionHandler().makeLastFollower(*emptyDrag);
    QMouseEvent move(QEvent::MouseMove, QPointF(20, 20), QPointF(20, 20), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move.ignore();
    emptyDrag->mouseMoveEvent(&move, state);
    QMouseEvent rel(QEvent::MouseButtonRelease, QPointF(20, 20), QPointF(20, 20), Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
    rel.ignore();
    emptyDrag->mouseReleaseEvent(&rel, state);
    QVERIFY(rel.isAccepted());
  }

  // Build a valid quad spline in image coordinates.
  auto makeQuad = []() {
    auto s = std::make_shared<EditableSpline>();
    s->appendVertex(QPointF(20, 20));
    s->appendVertex(QPointF(120, 20));
    s->appendVertex(QPointF(120, 120));
    s->appendVertex(QPointF(20, 120));
    s->setBridged(true);
    return s;
  };

  // P5-19: Drift-free ZoneDragInteraction across multiple moves and event accept on release.
  {
    auto quad = makeQuad();
    zones.addZone(quad);
    InteractionState state;
    const QPointF startWidget = view.imageToWidget().map(QPointF(70, 70));
    auto* drag = new ZoneDragInteraction(ctx, state, quad, false);
    view.rootInteractionHandler().makeLastFollower(*drag);
    const QPointF step1 = startWidget + QPointF(10, 6);
    const QPointF step2 = startWidget + QPointF(24, -8);
    QMouseEvent move1(QEvent::MouseMove, step1, view.mapToGlobal(step1), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move1.ignore();
    drag->mouseMoveEvent(&move1, state);
    QMouseEvent move2(QEvent::MouseMove, step2, view.mapToGlobal(step2), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move2.ignore();
    drag->mouseMoveEvent(&move2, state);
    QMouseEvent rel(QEvent::MouseButtonRelease, step2, view.mapToGlobal(step2), Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
    rel.ignore();
    drag->mouseReleaseEvent(&rel, state);
    QVERIFY(rel.isAccepted());
    zones.removeZone(quad);
  }

  // P5-20: ZoneContextMenuInteraction releases capture synchronously when menu hides.
  {
    auto quad = makeQuad();
    zones.addZone(quad);
    const QPointF centerWidget = view.imageToWidget().map(QPointF(70, 70));
    ctx.setLastContextMenuScreenPos(view.mapToGlobal(centerWidget.toPoint()));
    InteractionState state;
    ZoneContextMenuInteraction* menuInteraction = ZoneContextMenuInteraction::create(ctx, state);
    QVERIFY(menuInteraction != nullptr);
    QSignalSpy destroyedSpy(menuInteraction, &QObject::destroyed);
    view.rootInteractionHandler().makeLastFollower(*menuInteraction);
    QVERIFY(state.captured());
    QMenu* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
    QVERIFY(popup != nullptr);
    popup->close();
    QTRY_VERIFY(!state.captured());
    QTRY_COMPARE(destroyedSpy.count(), 1);
    zones.removeZone(quad);
  }

  // P5-19 & P5-22: ZoneVertexDragInteraction Ctrl align avoids /0 on coinciding neighbor and rolls back degenerate
  // zone.
  {
    auto tri = std::make_shared<EditableSpline>();
    tri->appendVertex(QPointF(20, 20));
    tri->appendVertex(QPointF(120, 20));
    tri->appendVertex(QPointF(120, 120));
    tri->setBridged(true);
    zones.addZone(tri);
    SplineVertex::Ptr v0 = tri->firstVertex();
    SplineVertex::Ptr v1 = v0->next(SplineVertex::NO_LOOP);
    SplineVertex::Ptr v2 = v1->next(SplineVertex::NO_LOOP);
    const QPointF origV2Pos = v2->point();

    InteractionState state;
    auto* vertexDrag = new ZoneVertexDragInteraction(ctx, state, tri, v2);
    view.rootInteractionHandler().makeLastFollower(*vertexDrag);
    // Drag v2 onto line between v0 and v1 with Ctrl so all 3 points become collinear (invalid zone).
    const QPointF cursorWidget(view.mapFromGlobal(QCursor::pos()));
    const QPointF collinearWidget
        = cursorWidget + (view.imageToWidget().map(QPointF(70, 20)) - view.imageToWidget().map(origV2Pos));
    QMouseEvent moveCollinear(QEvent::MouseMove, collinearWidget, collinearWidget, Qt::NoButton, Qt::LeftButton,
                              Qt::ControlModifier);
    moveCollinear.ignore();
    vertexDrag->mouseMoveEvent(&moveCollinear, state);
    QMouseEvent rel(QEvent::MouseButtonRelease, collinearWidget, collinearWidget, Qt::LeftButton, Qt::NoButton,
                    Qt::NoModifier);
    rel.ignore();
    vertexDrag->mouseReleaseEvent(&rel, state);
    QVERIFY(rel.isAccepted());
    QCOMPARE(v2->point(), origV2Pos);
    QCOMPARE(v0->point(), QPointF(20, 20));
    QCOMPARE(v1->point(), QPointF(120, 20));
    QVERIFY(Zone(SerializableSpline(*tri)).isValid());
    zones.removeZone(tri);

    // Ctrl align when moving near neighbor at (120, 20) on quad without division by zero.
    auto quad = makeQuad();
    zones.addZone(quad);
    SplineVertex::Ptr quadV2 = quad->firstVertex()->next(SplineVertex::NO_LOOP)->next(SplineVertex::NO_LOOP);
    auto* ctrlDrag = new ZoneVertexDragInteraction(ctx, state, quad, quadV2);
    view.rootInteractionHandler().makeLastFollower(*ctrlDrag);
    const QPointF coincideWidget
        = cursorWidget + (view.imageToWidget().map(QPointF(120, 20)) - view.imageToWidget().map(quadV2->point()));
    QMouseEvent moveCtrl(QEvent::MouseMove, coincideWidget, coincideWidget, Qt::NoButton, Qt::LeftButton,
                         Qt::ControlModifier);
    moveCtrl.ignore();
    ctrlDrag->mouseMoveEvent(&moveCtrl, state);
    QVERIFY(std::isfinite(quadV2->point().x()) && std::isfinite(quadV2->point().y()));
    QMouseEvent relCtrl(QEvent::MouseButtonRelease, coincideWidget, coincideWidget, Qt::LeftButton, Qt::NoButton,
                        Qt::NoModifier);
    relCtrl.ignore();
    ctrlDrag->mouseReleaseEvent(&relCtrl, state);
    zones.removeZone(quad);
  }

  // P5-18: ZoneDefaultInteraction remains safe under captured state.
  {
    auto quad = makeQuad();
    zones.addZone(quad);
    ZoneDefaultInteraction def(ctx);
    InteractionState state;
    InteractionState::Captor cap;
    state.capture(cap);
    const QPointF v0Widget = view.imageToWidget().map(QPointF(20, 20));
    def.proximityUpdate(v0Widget, state);
    QCOMPARE(std::distance(zones.begin(), zones.end()), std::ptrdiff_t(1));
  }
}
