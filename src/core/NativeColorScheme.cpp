// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "NativeColorScheme.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOptionSpinBox>

namespace {
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
/**
 * The Windows 11 style puts the two arrows of a spin box next to each other, which makes each
 * spin box about 40 pixels wider than in the other color schemes, so rows of spin boxes don't
 * fit into the options panels.  This puts the arrows above each other, as the other styles do.
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
    // The height as without buttons.  The width is the text, the margins of the text field (4 on
    // each side in this style), the frame and one column for both buttons, without the wide
    // extra margins of the style.
    QStyleOptionSpinBox withoutButtons(*spinBox);
    withoutButtons.buttonSymbols = QAbstractSpinBox::NoButtons;
    QSize result = QProxyStyle::sizeFromContents(type, &withoutButtons, size, widget);
    const int frameWidth = spinBox->frame ? pixelMetric(PM_SpinBoxFrameWidth, spinBox, widget) : 0;
    result.setWidth(size.width() + 10 + 2 * frameWidth + buttonColumnWidth(*spinBox));
    return result;
  }

  void polish(QWidget* widget) override {
    QProxyStyle::polish(widget);
    // Number fields keep their width instead of filling a widened panel.
    if (auto* spinBox = qobject_cast<QAbstractSpinBox*>(widget)) {
      spinBox->setSizePolicy(QSizePolicy::Fixed, spinBox->sizePolicy().verticalPolicy());
    }
  }

  using QProxyStyle::polish;

 private:
  static int buttonColumnWidth(const QStyleOptionSpinBox& spinBox) { return spinBox.fontMetrics.height(); }
};
#endif
}  // namespace

NativeColorScheme::NativeColorScheme() {
  loadStyleSheet();
  loadColorParams();
}

QStyle* NativeColorScheme::getStyle() const {
#if QT_VERSION >= QT_VERSION_CHECK(6, 1, 0)
  // The scheme is set once at startup, so the application still has the platform's style.
  if (qApp->style()->name() == QLatin1String("windows11")) {
    if (QStyle* windows11 = QStyleFactory::create(QStringLiteral("windows11"))) {
      return new CompactSpinBoxStyle(windows11);
    }
  }
#endif
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
  const QPalette palette = QPalette();

  m_styleSheet.append(QString("QGraphicsView, ImageViewBase, QFrame#imageViewFrame {\n"
                              "  background-color: %1;\n"
                              "  background-attachment: scroll;\n"
                              "}")
                          .arg(palette.window().color().darker(115).name()));
}

void NativeColorScheme::loadColorParams() {
  const QPalette palette = QPalette();

  m_customColors["ThumbnailSequenceSelectedItemBackground"] = palette.color(QPalette::Highlight).lighter(130);
  m_customColors["ThumbnailSequenceSelectionLeaderBackground"] = palette.color(QPalette::Highlight);
  m_customColors["ProcessingIndicationFade"] = palette.window().color().darker(115);
  if (palette.window().color().lightnessF() < 0.5) {
    // If system scheme is dark, adapt some colors.
    m_customColors["ProcessingIndicationHead"] = palette.color(QPalette::Window).lighter(200);
    m_customColors["ProcessingIndicationTail"] = palette.color(QPalette::Window).lighter(130);
    m_customColors["StageListHead"] = m_customColors.at("ProcessingIndicationHead");
    m_customColors["StageListTail"] = m_customColors.at("ProcessingIndicationTail");
  }
}
