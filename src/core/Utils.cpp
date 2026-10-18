// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Utils.h"

#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFontMetrics>
#include <QGridLayout>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSpinBox>
#include <QTextDocument>
#include <QWidget>
#include <algorithm>
#include <cmath>

#include "ApplicationSettings.h"

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <stdio.h>
#endif

namespace core {
#ifdef Q_OS_WIN
namespace {
/**
 * The path in the form that lets the Windows API handle more than MAX_PATH (260) characters,
 * whether or not long paths are enabled in Windows: absolute, with backslashes and "\\?\".
 */
std::wstring longWindowsPath(const QString& path) {
  QString native = QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
  if (native.startsWith(QLatin1String("\\\\?\\"))) {
    return native.toStdWString();
  }
  if (native.startsWith(QLatin1String("\\\\"))) {
    // A network path: \\server\share\... becomes \\?\UNC\server\share\...
    native = QLatin1String("\\\\?\\UNC\\") + native.mid(2);
  } else {
    native = QLatin1String("\\\\?\\") + native;
  }
  return native.toStdWString();
}
}  // namespace
#endif

bool Utils::overwritingRename(const QString& from, const QString& to) {
#ifdef Q_OS_WIN
  return MoveFileExW(longWindowsPath(from).c_str(), longWindowsPath(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  return rename(QFile::encodeName(from).data(), QFile::encodeName(to).data()) == 0;
#endif
}

QString Utils::richTextForLink(const QString& label, const QString& target) {
  return QString::fromLatin1(
             "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.0//EN\""
             "\"http://www.w3.org/TR/REC-html40/strict.dtd\">"
             "<html><head><meta name=\"qrichtext\" content=\"1\" />"
             "</head><body><p style=\"margin-top:0px; margin-bottom:0px;"
             "margin-left:0px; margin-right:0px; -qt-block-indent:0;"
             "text-indent:0px;\"><a href=\"%1\">%2</a></p></body></html>")
      .arg(target.toHtmlEscaped(), label.toHtmlEscaped());
}

void Utils::maybeCreateCacheDir(const QString& outputDir) {
  QDir(outputDir).mkdir(QString::fromLatin1("cache"));

  // QDir::mkdir() returns false if the directory already exists,
  // so to prevent confusion this function return void.
}

QString Utils::outputDirToThumbDir(const QString& outputDir) {
  return outputDir + QLatin1String("/cache/thumbs");
}

std::shared_ptr<ThumbnailPixmapCache> Utils::createThumbnailCache(const QString& outputDir) {
  const QSize maxPixmapSize = ApplicationSettings::getInstance().getThumbnailQuality();
  const QString thumbsCachePath(outputDirToThumbDir(outputDir));
  return std::make_shared<ThumbnailPixmapCache>(thumbsCachePath, maxPixmapSize, 40, 5);
}

QString Utils::qssConvertPxToEm(const QString& stylesheet, const double base, const int precision) {
  QString result = "";
  const QRegularExpression pxToEm(R"((\d+(\.\d+)?)px)");

  QRegularExpressionMatchIterator iter = pxToEm.globalMatch(stylesheet);
  int prevIndex = 0;
  while (iter.hasNext()) {
    QRegularExpressionMatch match = iter.next();
    result.append(stylesheet.mid(prevIndex, match.capturedStart() - prevIndex));

    double value = match.captured(1).toDouble();
    value /= base;
    result.append(QString::number(value, 'f', precision)).append("em");

    prevIndex = match.capturedEnd();
  }
  result.append(stylesheet.mid(prevIndex));
  return result;
}

QString Utils::qssApplyAccentColor(const QString& stylesheet, const QString& defaultAccentColor) {
  const QColor accent = ApplicationSettings::getInstance().getAccentColor();
  if (!accent.isValid()) {
    return stylesheet;
  }
  const QString accentName = accent.name();

  QString result = stylesheet;
  result.replace(defaultAccentColor, accentName, Qt::CaseInsensitive);
  // The selection color is left as it is; only the progress bar takes the accent color.
  const QRegularExpression progressBarColor(R"((QProgressBar::chunk\s*\{[^}]*background-color:\s*)#[0-9a-fA-F]{6})");
  result.replace(progressBarColor, QStringLiteral("\\1") + accentName);
  return result;
}

QString Utils::dpiText(const int dpi) {
  return QStringLiteral("%1 dpi").arg(dpi);
}

int Utils::dpiFromText(const QString& text, bool* ok) {
  const QRegularExpressionMatch match = QRegularExpression(QStringLiteral("\\d+")).match(text);
  if (!match.hasMatch()) {
    *ok = false;
    return 0;
  }
  return match.captured().toInt(ok);
}

void Utils::setDpiFieldWidth(QComboBox* field) {
  // The size hint of an editable list leaves out the padding the style sheets give its text field,
  // so the longest entry would be cut off by a few pixels. A fixed width doesn't help, as the
  // style sheet's min-width replaces the minimum width again. Instead the size hint is enlarged:
  // a minimum contents length (counted in widths of "X") covering the longest entry plus about
  // two thirds of a digit.
  const QFontMetrics metrics(field->fontMetrics());
  int widest = 0;
  for (int i = 0; i < field->count(); ++i) {
    widest = std::max(widest, metrics.horizontalAdvance(field->itemText(i)));
  }
  widest += (2 * metrics.horizontalAdvance(QLatin1Char('0')) + 2) / 3;
  const int charWidth = std::max(1, metrics.horizontalAdvance(QLatin1Char('X')));
  field->setSizeAdjustPolicy(QComboBox::AdjustToContents);
  field->setMinimumContentsLength((widest + charWidth - 1) / charWidth);
}

void Utils::setNumberFieldWidth(QAbstractSpinBox* field, const double widestValue, const int decimals) {
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
  // padding and the arrows is measured, and the text is added with the text field's margins of
  // 2 pixels on each side and 2 pixels for the cursor.
  const QRect editField
      = field->style()->subControlRect(QStyle::CC_SpinBox, &option, QStyle::SC_SpinBoxEditField, field);
  const int overhead = option.rect.width() - editField.width();
  field->setFixedWidth(field->fontMetrics().horizontalAdvance(text) + 6 + overhead);
}

void Utils::alignLabelColumns(const QList<QGridLayout*>& grids, const int column) {
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

QValidator* Utils::createDpiValidator(QObject* parent) {
  return new QRegularExpressionValidator(
      QRegularExpression(QStringLiteral(R"(\s*\d{0,4}\s*(d(p(i)?)?)?\s*)"), QRegularExpression::CaseInsensitiveOption),
      parent);
}
}  // namespace core
