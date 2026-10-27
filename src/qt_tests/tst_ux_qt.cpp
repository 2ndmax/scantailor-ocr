// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "tst_ux_qt.h"

#include <ApplicationSettings.h>
#include <CollapsibleGroupBox.h>
#include <Dpi.h>
#include <ErrorWidget.h>
#include <ImageId.h>
#include <ImageTransformation.h>
#include <NativeColorScheme.h>
#include <StyleSheetColorScheme.h>
#include <StyledIconPack.h>
#include <ThumbnailBase.h>
#include <ThumbnailPixmapCache.h>
#include <color_schemes/dark_scheme_colors.h>
#include <color_schemes/light_scheme_colors.h>

#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLabel>
#include <QList>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
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
  QElapsedTimer wait;
  wait.start();
  QPixmap loaded;
  while (cache.loadRequest(id, loaded, {}) != ThumbnailPixmapCache::LOADED && wait.elapsed() < 10000) {
    QCoreApplication::processEvents();
    QThread::msleep(10);
  }
  return !loaded.isNull();
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
