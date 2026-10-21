// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OcrPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <map>
#include <vector>

#include "OcrEngine.h"
#include "OcrLanguages.h"
#include "TessdataDownloadDialog.h"

namespace {
/** The language list grows with the number of languages up to this many rows, then it scrolls. */
const int MAX_VISIBLE_LANGUAGES = 8;
}  // namespace

OcrPanel::OcrPanel(QWidget* parent) : QWidget(parent) {
  auto* ocrGroupLayout = new QVBoxLayout(this);
  ocrGroupLayout->setContentsMargins(0, 0, 0, 0);
  m_ocrEnabled = new QCheckBox(tr("Recognize text"));
  m_ocrEnabled->setToolTip(
      tr("Adds an invisible text layer, so the text of the PDF can be searched, selected and copied. Text "
         "recognition takes a few seconds per page."));
  // The button shares the line of the check box, as in the Automatic Import panel. It stays
  // usable while text recognition is off, so languages can be added first.
  auto* moreLanguagesButton = new QPushButton(tr("More languages ..."));
  moreLanguagesButton->setToolTip(tr("Download more languages from the internet."));
  moreLanguagesButton->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  auto* ocrEnabledRow = new QHBoxLayout;
  ocrEnabledRow->addWidget(m_ocrEnabled, 1);
  ocrEnabledRow->addWidget(moreLanguagesButton);
  ocrGroupLayout->addLayout(ocrEnabledRow);
  // Languages of the project that are missing here are ticked in the dialog already.
  connect(moreLanguagesButton, &QPushButton::clicked, this, [this]() { downloadLanguages(missingLanguages()); });

  // Greyed out while text recognition is off, and indented below its check box.
  m_ocrOptions = new QWidget;
  auto* ocrLayout = new QVBoxLayout(m_ocrOptions);
  ocrLayout->setContentsMargins(15, 0, 0, 0);
  ocrGroupLayout->addWidget(m_ocrOptions);
  ocrLayout->addWidget(new QLabel(tr("Languages:")));
  m_languageList = new QListWidget;
  m_languageList->setSelectionMode(QAbstractItemView::NoSelection);
  m_languageList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  // Some room above the first and below the last language; the rest of the look comes from the color scheme.
  m_languageList->setStyleSheet(QStringLiteral("QListView { padding-top: 7px; padding-bottom: 7px; }"));
  m_languageList->setToolTip(tr("Tick the languages of the text. Several languages can be ticked."));
  ocrLayout->addWidget(m_languageList);

  QStringList dirs;
  for (const QString& dir : OcrLanguages::searchDirs()) {
    dirs.push_back(QDir::toNativeSeparators(dir));
  }
  m_noLanguagesLabel = new QLabel(tr("No language files were found. Download them with \"More languages\", or "
                                     "put *.traineddata files (from tessdata_best) into one of these folders:")
                                  + QStringLiteral("\n") + dirs.join('\n'));
  m_noLanguagesLabel->setWordWrap(true);
  m_noLanguagesLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  ocrLayout->addWidget(m_noLanguagesLabel);

  auto* layoutRow = new QFormLayout;
  layoutRow->setRowWrapPolicy(QFormLayout::WrapLongRows);
  layoutRow->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);
  m_pageLayout = new QComboBox;
  m_pageLayout->addItem(tr("Automatic"), static_cast<int>(OcrEngine::AUTOMATIC_LAYOUT));
  m_pageLayout->addItem(tr("Single column"), static_cast<int>(OcrEngine::SINGLE_COLUMN));
  m_pageLayout->addItem(tr("Single block of text"), static_cast<int>(OcrEngine::SINGLE_BLOCK));
  const QString textLayoutTip = tr(
      "How the text is arranged on the pages. This decides the order in which the text is recognized, "
      "e.g. for searching and copying; the look of the PDF doesn't change. \"Automatic\" detects columns, "
      "pictures and captions and is usually right. \"Single column\" helps if copied text comes out in the "
      "wrong order, e.g. with indented lines or tables. \"Single block of text\" suits pages with only one "
      "short text, such as a label or a note.");
  m_pageLayout->setToolTip(textLayoutTip);
  auto* textLayoutLabel = new QLabel(tr("Text layout:"));
  textLayoutLabel->setToolTip(textLayoutTip);
  layoutRow->addRow(textLayoutLabel, m_pageLayout);
  ocrLayout->addLayout(layoutRow);

  setOptions(ProcessingOptions::Pdf());

  connect(m_ocrEnabled, &QCheckBox::toggled, m_ocrOptions, &QWidget::setEnabled);
  connect(m_ocrEnabled, &QCheckBox::toggled, this, &OcrPanel::optionsChanged);
  connect(m_languageList, &QListWidget::itemChanged, this, &OcrPanel::languageChanged);
  connect(m_pageLayout, qOverload<int>(&QComboBox::currentIndexChanged), this, &OcrPanel::optionsChanged);
}

OcrPanel::~OcrPanel() = default;

void OcrPanel::setOptions(const ProcessingOptions::Pdf& pdf) {
  const QSignalBlocker enabledBlocker(m_ocrEnabled);
  const QSignalBlocker layoutBlocker(m_pageLayout);
  m_ocrEnabled->setChecked(pdf.ocr);
  m_ocrOptions->setEnabled(pdf.ocr);
  fillLanguageList(pdf.ocrLanguages);
  m_pageLayout->setCurrentIndex(std::max(0, m_pageLayout->findData(pdf.ocrPageLayout)));
}

void OcrPanel::storeOptions(ProcessingOptions::Pdf& pdf) const {
  pdf.ocr = m_ocrEnabled->isChecked();
  pdf.ocrLanguages = tickedLanguages(true);
  pdf.ocrPageLayout = pageLayout();
}

bool OcrPanel::isOcrEnabled() const {
  return m_ocrEnabled->isChecked();
}

int OcrPanel::pageLayout() const {
  return m_pageLayout->currentData().toInt();
}

QStringList OcrPanel::checkedLanguages() const {
  return tickedLanguages(false);
}

QStringList OcrPanel::missingLanguages() const {
  QStringList codes;
  for (int row = 0; row < m_languageList->count(); ++row) {
    const QListWidgetItem* item = m_languageList->item(row);
    if (item->data(MISSING_ROLE).toBool()) {
      codes.push_back(item->data(Qt::UserRole).toString());
    }
  }
  return codes;
}

QString OcrPanel::missingLanguageNames() const {
  QStringList names;
  for (const QString& code : missingLanguages()) {
    names.push_back(OcrLanguages::displayName(code));
  }
  return names.join(QStringLiteral(", "));
}

QStringList OcrPanel::tickedLanguages(const bool withMissing) const {
  QStringList codes;
  for (int row = 0; row < m_languageList->count(); ++row) {
    const QListWidgetItem* item = m_languageList->item(row);
    if ((item->checkState() == Qt::Checked) && (withMissing || !item->data(MISSING_ROLE).toBool())) {
      codes.push_back(item->data(Qt::UserRole).toString());
    }
  }
  return codes;
}

void OcrPanel::languageChanged(QListWidgetItem* item) {
  if (item->data(MISSING_ROLE).toBool() && (item->checkState() != Qt::Checked)) {
    // A missing language that is unticked leaves the project; it can't be ticked again without its file.
    // Removed later, as the list is still busy with the item.
    const QString code = item->data(Qt::UserRole).toString();
    QTimer::singleShot(0, this, [this, code]() {
      QStringList codes = tickedLanguages(true);
      codes.removeAll(code);
      fillLanguageList(codes);
      emit optionsChanged();
    });
    return;
  }
  emit optionsChanged();
}

void OcrPanel::fillLanguageList(const QStringList& checked) {
  // Languages sorted by name, then the script models.
  struct Language {
    bool isScript;
    QString name;
    QString code;
    bool missing;
  };
  std::vector<Language> languages;
  const std::map<QString, QString> installed = OcrLanguages::available();
  for (const auto& entry : installed) {
    languages.push_back(
        {OcrLanguages::isScript(entry.first), OcrLanguages::displayName(entry.first), entry.first, false});
  }
  // Chosen languages without a file here are listed too, ticked and marked.
  for (const QString& code : checked) {
    if ((installed.count(code) == 0)
        && std::none_of(languages.begin(), languages.end(), [&code](const Language& l) { return l.code == code; })) {
      languages.push_back({OcrLanguages::isScript(code), OcrLanguages::displayName(code), code, true});
    }
  }
  std::sort(languages.begin(), languages.end(), [](const Language& a, const Language& b) {
    if (a.isScript != b.isScript) {
      return !a.isScript;
    }
    return QString::localeAwareCompare(a.name, b.name) < 0;
  });

  {
    // Filling the list isn't a change by the user.
    const QSignalBlocker blocker(m_languageList);
    m_languageList->clear();
    for (const Language& language : languages) {
      // The warning sign of the PDF step; emoji fonts show it in color.
      const QString text = language.missing ? (QString(QChar(0x26A0)) + ' ' + language.name) : language.name;
      auto* item = new QListWidgetItem(text, m_languageList);
      item->setData(Qt::UserRole, language.code);
      item->setData(MISSING_ROLE, language.missing);
      item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
      item->setCheckState(checked.contains(language.code) ? Qt::Checked : Qt::Unchecked);
      if (language.missing) {
        item->setToolTip(
            tr("The file of this language is missing on this computer. Download it with \"More languages\". The "
               "language is kept in the project."));
      }
    }
  }
  m_noLanguagesLabel->setVisible(installed.empty());
  updateLanguageListHeight();
}

void OcrPanel::updateLanguageListHeight() {
  const int count = m_languageList->count();
  m_languageList->setVisible(count > 0);
  if (count == 0) {
    return;
  }
  int rowHeight = m_languageList->sizeHintForRow(0);
  if (rowHeight <= 0) {
    rowHeight = m_languageList->fontMetrics().height() + 4;
  }
  const int rows = std::min(count, MAX_VISIBLE_LANGUAGES);
  // The contents margins hold the border and the padding of the style sheet as drawn;
  // frameWidth() can be larger, which left extra space below the last row.
  m_languageList->ensurePolished();
  const QMargins margins = m_languageList->contentsMargins();
  m_languageList->setFixedHeight(rows * rowHeight + margins.top() + margins.bottom());
}

void OcrPanel::downloadLanguages(const QStringList& preselected) {
  TessdataDownloadDialog dialog(this);
  dialog.preselect(preselected);
  dialog.exec();
  if (!dialog.downloadedCodes().isEmpty()) {
    // New languages are ticked right away; they were downloaded to be used.
    fillLanguageList(tickedLanguages(true) + dialog.downloadedCodes());
    emit optionsChanged();
  }
}

void OcrPanel::downloadMissingLanguages() {
  downloadLanguages(missingLanguages());
}
