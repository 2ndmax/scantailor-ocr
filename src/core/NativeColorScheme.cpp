// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "NativeColorScheme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionSpinBox>

#include "StyleSheetColorScheme.h"
#include "color_schemes/dark_scheme_colors.h"
#include "color_schemes/light_scheme_colors.h"

namespace {
/** The background of the image views: a little darker than the window. */
QColor imageViewColor(const QPalette& palette) {
  return palette.window().color().darker(115);
}

/** A shade off the window color, to stand out from it: the tabs that are not selected and the grips between the panels.
 */
QColor offWindowColor(const QPalette& palette, const bool hovered) {
  const QColor window = palette.window().color();
  const bool darkWindow = window.lightnessF() < 0.5;
  if (darkWindow) {
    return window.lighter(hovered ? 190 : 160);
  }
  return window.darker(hovered ? 120 : 110);
}

/**
 * The Windows 11 style puts the two arrows of a spin box next to each other, which makes each
 * spin box about 40 pixels wider than in the other color schemes, so rows of spin boxes don't
 * fit into the options panels. This puts the arrows above each other, as the other styles do.
 */
class CompactSpinBoxStyle : public QProxyStyle {
 public:
  explicit CompactSpinBoxStyle(QStyle* baseStyle) : QProxyStyle(baseStyle) {}

  QRect subControlRect(const ComplexControl control,
                       const QStyleOptionComplex* option,
                       const SubControl subControl,
                       const QWidget* widget) const override {
    const auto* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
    if ((control != CC_SpinBox) || !spinBox || (spinBox->buttonSymbols == QAbstractSpinBox::NoButtons)) {
      return QProxyStyle::subControlRect(control, option, subControl, widget);
    }

    const int frameWidth = spinBox->frame ? pixelMetric(PM_SpinBoxFrameWidth, spinBox, widget) : 0;
    const QRect inner = spinBox->rect.adjusted(frameWidth, frameWidth, -frameWidth, -frameWidth);
    const int buttonWidth = buttonColumnWidth(*spinBox);
    const int upHeight = inner.height() / 2;
    QRect rect;
    switch (subControl) {
      case SC_SpinBoxUp:
        rect = QRect(inner.right() - buttonWidth + 1, inner.top(), buttonWidth, upHeight);
        break;
      case SC_SpinBoxDown:
        rect = QRect(inner.right() - buttonWidth + 1, inner.top() + upHeight, buttonWidth, inner.height() - upHeight);
        break;
      case SC_SpinBoxEditField:
        rect = inner.adjusted(0, 0, -buttonWidth, 0);
        break;
      default:
        return QProxyStyle::subControlRect(control, option, subControl, widget);
    }
    return visualRect(spinBox->direction, spinBox->rect, rect);
  }

  QSize sizeFromContents(const ContentsType type,
                         const QStyleOption* option,
                         const QSize& size,
                         const QWidget* widget) const override {
    const auto* spinBox = qstyleoption_cast<const QStyleOptionSpinBox*>(option);
    if ((type != CT_SpinBox) || !spinBox || (spinBox->buttonSymbols == QAbstractSpinBox::NoButtons)) {
      return QProxyStyle::sizeFromContents(type, option, size, widget);
    }
    // The height as without buttons. The width is the text, the margins of the text field (4 on
    // each side in this style), the frame and one column for both buttons, without the wide
    // extra margins of the style.
    QStyleOptionSpinBox withoutButtons(*spinBox);
    withoutButtons.buttonSymbols = QAbstractSpinBox::NoButtons;
    QSize result = QProxyStyle::sizeFromContents(type, &withoutButtons, size, widget);
    const int frameWidth = spinBox->frame ? pixelMetric(PM_SpinBoxFrameWidth, spinBox, widget) : 0;
    result.setWidth(size.width() + 10 + 2 * frameWidth + buttonColumnWidth(*spinBox));
    return result;
  }

 private:
  static int buttonColumnWidth(const QStyleOptionSpinBox& spinBox) { return spinBox.fontMetrics.height(); }
};
}  // namespace

NativeColorScheme::NativeColorScheme() {
  loadStyleSheet();
  loadColorParams();
}

QStyle* NativeColorScheme::getStyle() const {
  // The scheme is set once at startup, so the application still has the platform's style.
  if (qApp->style()->name() == QLatin1String("windows11")) {
    if (QStyle* windows11 = QStyleFactory::create(QStringLiteral("windows11"))) {
      return new CompactSpinBoxStyle(windows11);
    }
  }
  return nullptr;
}

const QPalette* NativeColorScheme::getPalette() const {
  return nullptr;
}

const QString* NativeColorScheme::getStyleSheet() const {
  return &m_styleSheet;
}

const ColorScheme::ColorParams* NativeColorScheme::getColorParams() const {
  return &m_customColors;
}

void NativeColorScheme::loadStyleSheet() {
  m_styleSheet.append(QString("QGraphicsView, ImageViewBase, QFrame#imageViewFrame {\n"
                              "  background-color: %1;\n"
                              "  background-attachment: scroll;\n"
                              "}")
                          .arg(imageViewColor(QPalette()).name()));
  // The tabs of the output views, in the same shape as in the Light and Dark schemes (stylesheet.qss): separate
  // buttons, the selected one filled with the accent color. The native style shows the selected tab only by a
  // slight shade. Unlike there, no border, and the colors come from the system palette; change both places.
  const QPalette palette;
  m_styleSheet.append(QString("\n"
                              "output--TabbedImageView > QTabBar::tab {\n"
                              "  padding: 9px 3px 9px 3px;\n"
                              "  min-height: 17px;\n"
                              "  min-width: 17px;\n"
                              "  margin: 2px 0px 2px 0px;\n"
                              "  border: none;\n"
                              "  background-color: %1;\n"
                              "}\n"
                              "\n"
                              "output--TabbedImageView > QTabBar::tab:hover {\n"
                              "  background-color: %2;\n"
                              "}\n"
                              "\n"
                              "output--TabbedImageView > QTabBar::tab:selected {\n"
                              "  color: palette(highlighted-text);\n"
                              "  background-color: palette(highlight);\n"
                              "}")
                          .arg(offWindowColor(palette, false).name(), offWindowColor(palette, true).name()));
  // The grips between the panels, as in the Light and Dark schemes (stylesheet.qss): a bar, in the accent color
  // under the pointer. Change both places.
  m_styleSheet.append(QString("\n"
                              "QMainWindow::separator {\n"
                              "  background-color: %1;\n"
                              "  width: 5px;\n"
                              "  height: 5px;\n"
                              "}\n"
                              "\n"
                              "QMainWindow::separator:hover {\n"
                              "  background-color: palette(highlight);\n"
                              "}")
                          .arg(offWindowColor(palette, false).name()));
}

void NativeColorScheme::loadColorParams() {
  // The same meanings as in Light and Dark, taken from the system's palette.
  const QPalette palette = QPalette();
  StyleSheetColorScheme::Colors colors;
  colors.text = palette.color(QPalette::Text);
  colors.border = palette.color(QPalette::Mid);
  colors.imageView = imageViewColor(palette);
  // The palette has no error color: the one of Light or Dark, whichever matches the window.
  const bool darkWindow = palette.color(QPalette::Window).lightnessF() < 0.5;
  colors.error = QColor(darkWindow ? dark_scheme_colors::error : light_scheme_colors::error);
  m_customColors = StyleSheetColorScheme::colorParams(colors, palette.color(QPalette::Highlight),
                                                      palette.color(QPalette::HighlightedText));
}
