// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "WidgetUtils.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLineEdit>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionComboBox>
#include <QStyleOptionFrame>
#include <QStyleOptionSpinBox>
#include <QWidget>
#include <algorithm>

#include "Utils.h"

namespace core {
namespace {
/// The width of the text field of a number field or an editable list that shows the given text:
/// the text with the text field's margins of 2 pixels on each side and 2 pixels for the cursor.
/// Some styles, such as the Windows 11 style, also draw the text with padding inside the text
/// field; it is measured as QLineEdit::paintEvent() places the text, so the last character isn't
/// cut off.
int textFieldWidth(const QWidget* field, QLineEdit* edit, const QString& text) {
  const int width = field->fontMetrics().horizontalAdvance(text) + 6;
  if (!edit) {
    return width;
  }
  edit->ensurePolished();
  QStyleOptionFrame frame;
  frame.initFrom(edit);
  frame.lineWidth = edit->hasFrame() ? edit->style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &frame, edit) : 0;
  const QRect contents = edit->style()->subElementRect(QStyle::SE_LineEditContents, &frame, edit);
  const QMargins margins = edit->textMargins();
  return width + std::max(0, frame.rect.width() - contents.width() + margins.left() + margins.right());
}
}  // namespace

void WidgetUtils::setDpiFieldWidth(QComboBox* field) {
  // As wide as three digits, such as "600 dpi"; four digits can still be typed in, and the list
  // itself is as wide as its entries. The style's size hint is far wider than needed, so the
  // space taken by the frame, the padding and the arrow is measured, as for number fields. The
  // style sheets' min-width of lists is smaller, so it doesn't undo the width.
  field->ensurePolished();
  QStyleOptionComboBox option;
  option.initFrom(field);
  option.editable = field->isEditable();
  option.frame = field->hasFrame();
  const QRect editField
      = field->style()->subControlRect(QStyle::CC_ComboBox, &option, QStyle::SC_ComboBoxEditField, field);
  const int overhead = option.rect.width() - editField.width();
  field->setFixedWidth(textFieldWidth(field, field->lineEdit(), Utils::dpiText(999)) + overhead);
}

void WidgetUtils::setNumberFieldWidth(QAbstractSpinBox* field, const double widestValue, const int decimals) {
  QString prefix;
  QString suffix;
  int fieldDecimals = 0;
  if (const auto* doubleBox = qobject_cast<QDoubleSpinBox*>(field)) {
    prefix = doubleBox->prefix();
    suffix = doubleBox->suffix();
    fieldDecimals = doubleBox->decimals();
  } else if (const auto* intBox = qobject_cast<QSpinBox*>(field)) {
    prefix = intBox->prefix();
    suffix = intBox->suffix();
  }
  // Usually called before the field is shown: the style sets its font and margins on polishing.
  field->ensurePolished();
  QLocale locale = field->locale();
  locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
  const QString text = prefix + locale.toString(widestValue, 'f', (decimals >= 0) ? decimals : fieldDecimals) + suffix;
  QStyleOptionSpinBox option;
  option.initFrom(field);
  option.buttonSymbols = field->buttonSymbols();
  option.subControls = QStyle::SC_SpinBoxFrame | QStyle::SC_SpinBoxEditField;
  if (field->buttonSymbols() != QAbstractSpinBox::NoButtons) {
    option.subControls |= QStyle::SC_SpinBoxUp | QStyle::SC_SpinBoxDown;
  }
  option.frame = field->hasFrame();
  // The style's size hint is far wider than needed. Instead the space taken by the frame, the
  // padding and the arrows is measured, and the text field is added.
  const QRect editField
      = field->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, field);
  const int overhead = option.rect.width() - editField.width();
  field->setFixedWidth(textFieldWidth(field, field->findChild<QLineEdit*>(), text) + overhead);
}

void WidgetUtils::setUpLengthField(QDoubleSpinBox* field, const Units units, const double widestValue) {
  const bool fineSteps = (units == CENTIMETRES) || (units == INCHES);
  field->setDecimals(fineSteps ? 2 : ((units == PIXELS) ? 0 : 1));
  field->setSingleStep(fineSteps ? 0.01 : 1.0);
  field->setSuffix(QChar(' ') + unitsToLocalizedString(units));
  setNumberFieldWidth(field, widestValue);
}

void WidgetUtils::alignLabelColumns(const QList<QGridLayout*>& grids, const int column) {
  int width = 0;
  for (const QGridLayout* grid : grids) {
    for (int row = 0; row < grid->rowCount(); ++row) {
      QLayoutItem* item = grid->itemAtPosition(row, column);
      if (!item) {
        continue;
      }
      // Items spanning several columns, such as sliders below their labels, don't count.
      int itemRow = 0;
      int itemColumn = 0;
      int rowSpan = 0;
      int columnSpan = 0;
      grid->getItemPosition(grid->indexOf(item), &itemRow, &itemColumn, &rowSpan, &columnSpan);
      if (columnSpan == 1) {
        // The style sheet may still change the font; the width is measured with the final one.
        QWidget* widget = item->widget();
        if (widget) {
          widget->ensurePolished();
        }
        width = std::max(width, widget ? widget->sizeHint().width() : item->sizeHint().width());
      }
    }
  }
  // A grid without its own spacing takes that of its parent layout, which may differ between the
  // grids; all get the spacing of the first one.
  const int spacing = grids.isEmpty() ? 0 : grids.first()->horizontalSpacing();
  for (QGridLayout* grid : grids) {
    grid->setColumnMinimumWidth(column, width);
    grid->setHorizontalSpacing(spacing);
  }
}

QValidator* WidgetUtils::createDpiValidator(QObject* parent) {
  return new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral(R"(\s*\d{0,4}\s*(d(p(i)?)?)?\s*)"), QRegularExpression::CaseInsensitiveOption),
      parent);
}
}  // namespace core
