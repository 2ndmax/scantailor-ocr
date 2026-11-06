// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ExportView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHash>
#include <QHelpEvent>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMargins>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStyledItemDelegate>
#include <QThread>
#include <QTimer>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>
#include <algorithm>
#include <functional>
#include <map>
#include <tuple>
#include <utility>

#include "Application.h"
#include "ApplicationSettings.h"
#include "CollapsibleGroupBox.h"
#include "ColorSchemeManager.h"
#include "ExportFormatsPanel.h"
#include "ExportJob.h"
#include "ImageId.h"
#include "OcrEngine.h"
#include "OcrLanguages.h"
#include "OcrPanel.h"
#include "PathNames.h"
#include "PdfCompressionPanel.h"
#include "PdfPageOrder.h"
#include "SectionHeading.h"
#include "ThumbnailBase.h"
#include "ThumbnailLoadResult.h"

namespace {
/** The area of a tile the thumbnail is fitted into. */
const QSize THUMBNAIL_SIZE(120, 160);
const int TILE_MARGIN = 6;
const int TILE_WIDTH = THUMBNAIL_SIZE.width() + 4 * TILE_MARGIN;

/** Data of the items of the page list, besides the file name, thumbnail and tick. */
enum PageRole {
  ENTRY_INDEX_ROLE = Qt::UserRole,
  KEY_ROLE,
  /** The sign above the thumbnail, a PageSign. */
  SIGN_ROLE,
  /** The position of the page in the PDF, starting at 1; 0 if it's not in the PDF. */
  POSITION_ROLE
};

/** A warning outweighs information. */
enum PageSign { NO_SIGN, INFORMATION_SIGN, WARNING_SIGN };

// Characters that emoji fonts show in color, such as Segoe UI Emoji on Windows.
QString warningSign() {
  return QString(QChar(0x26A0));
}

/** With the emoji variation selector, as there is no colored character of its own. */
QString informationSign() {
  return QStringLiteral(u"ℹ️");
}

/** Whether the method chosen in a compression box uses the quality setting. */
bool usesQuality(const QComboBox* box) {
  const auto compression = static_cast<PdfCompression>(box->currentData().toInt());
  return (compression == PdfCompression::JPEG) || (compression == PdfCompression::JPEG2000);
}

/** The accent color of the color scheme: that of the frame of the selected thumbnail. */
QColor accentColor() {
  return ColorSchemeManager::instance().getColorParam("ThumbnailSequenceSelectedItemBorder");
}

/**
 * A button drawn as the others of the color scheme, with a frame in the accent color, as the frame
 * of the selected thumbnail: the main action of the step.
 */
class AccentFrameButton : public QPushButton {
 public:
  explicit AccentFrameButton(const QString& text) : QPushButton(text) {}

 protected:
  void paintEvent(QPaintEvent* event) override {
    QPushButton::paintEvent(event);
    if (!isEnabled()) {
      return;
    }
    const QColor accent = accentColor();
    const double radius = ColorSchemeManager::instance().hasRoundedCorners() ? 4.0 : 0.0;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(accent, 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), radius, radius);
  }
};
}  // namespace

/**
 * Draws a page of the list as a tile: tick, warning sign and position in the PDF on top, the
 * thumbnail in the middle, the file name below. The tooltip tells the rest.
 */
class ExportView::PageTileDelegate : public QStyledItemDelegate {
 public:
  /** param view Builds the tooltips when they are shown. */
  PageTileDelegate(const ExportView* view, QObject* parent) : QStyledItemDelegate(parent), m_view(view) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& /*index*/) const override {
    const int lineHeight = option.fontMetrics.height();
    return QSize(TILE_WIDTH, TILE_MARGIN + headerHeight(option) + TILE_MARGIN + THUMBNAIL_SIZE.height() + TILE_MARGIN
                                 + lineHeight + TILE_MARGIN);
  }

  void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
    const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
    const QPalette& palette = option.palette;
    const bool checkable = index.flags().testFlag(Qt::ItemIsUserCheckable);
    const bool included = checkable && (index.data(Qt::CheckStateRole).toInt() == Qt::Checked);
    const bool selected = option.state.testFlag(QStyle::State_Selected);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    // Square or rounded corners, as the buttons of the color scheme.
    const bool rounded = ColorSchemeManager::instance().hasRoundedCorners();

    // Frame; selected tiles have one in the accent color.
    const QRectF frame = QRectF(option.rect).adjusted(1.5, 1.5, -1.5, -1.5);
    const double frameRadius = rounded ? 4.0 : 0.0;
    painter->setBrush(Qt::NoBrush);
    painter->setPen(selected ? QPen(accentColor(), 2) : QPen(palette.color(QPalette::Mid), 1));
    painter->drawRoundedRect(frame, frameRadius, frameRadius);

    // Tick.
    if (checkable) {
      QStyleOptionButton check;
      check.rect = checkRect(option);
      check.state = QStyle::State_Enabled | (included ? QStyle::State_On : QStyle::State_Off);
      style->drawPrimitive(QStyle::PE_IndicatorCheckBox, &check, painter, option.widget);
    }

    // Position in the PDF.
    const int position = index.data(POSITION_ROLE).toInt();
    if (position > 0) {
      QFont font = option.font;
      font.setBold(true);
      const QFontMetrics metrics(font);
      const QString text = QString::number(position);
      const int height = headerHeight(option);
      const int width = std::max(height, metrics.horizontalAdvance(text) + 10);
      const QRect badge(option.rect.right() - TILE_MARGIN - width + 1, option.rect.top() + TILE_MARGIN, width, height);
      painter->setFont(font);
      painter->setPen(palette.color(QPalette::Text));
      painter->drawText(badge, Qt::AlignCenter, text);
    }

    // Warning or information sign, in the middle between tick and position.
    const int sign = index.data(SIGN_ROLE).toInt();
    const QRect header(option.rect.left(), option.rect.top() + TILE_MARGIN, option.rect.width(), headerHeight(option));
    if (sign != NO_SIGN) {
      QFont font = option.font;
      font.setBold(true);
      painter->setFont(font);
      painter->setPen(palette.color(QPalette::Text));
      painter->drawText(header, Qt::AlignCenter, (sign == WARNING_SIGN) ? warningSign() : informationSign());
    }

    // Pages left out of the PDF are drawn faded; pages not output yet are not, so that their
    // placeholder stays readable.
    if (checkable && !included) {
      painter->setOpacity(0.4);
    }

    // Thumbnail.
    const int thumbnailTop = option.rect.top() + TILE_MARGIN + headerHeight(option) + TILE_MARGIN;
    const QRect thumbnailRect(option.rect.left() + (option.rect.width() - THUMBNAIL_SIZE.width()) / 2, thumbnailTop,
                              THUMBNAIL_SIZE.width(), THUMBNAIL_SIZE.height());
    const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
    if (!icon.isNull()) {
      icon.paint(painter, thumbnailRect, Qt::AlignCenter);
    }

    // File name.
    const QRect textRect = nameRect(option);
    painter->setFont(option.font);
    painter->setPen(palette.color(QPalette::Text));
    painter->drawText(
        textRect, Qt::AlignHCenter | Qt::AlignVCenter,
        option.fontMetrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideMiddle, textRect.width()));

    painter->restore();
  }

  bool editorEvent(QEvent* event,
                   QAbstractItemModel* model,
                   const QStyleOptionViewItem& option,
                   const QModelIndex& index) override {
    if (!index.flags().testFlag(Qt::ItemIsUserCheckable)) {
      return false;
    }
    bool toggle = false;
    if ((event->type() == QEvent::MouseButtonRelease) || (event->type() == QEvent::MouseButtonDblClick)) {
      const auto* mouseEvent = static_cast<QMouseEvent*>(event);
      const QPoint pos = mouseEvent->position().toPoint();
      if ((mouseEvent->button() != Qt::LeftButton) || !checkRect(option).adjusted(-2, -2, 2, 2).contains(pos)) {
        return false;
      }
      // A double click toggles once (on release), not twice.
      toggle = (event->type() == QEvent::MouseButtonRelease);
      if (!toggle) {
        return true;
      }
    } else if (event->type() == QEvent::KeyPress) {
      const int key = static_cast<QKeyEvent*>(event)->key();
      toggle = (key == Qt::Key_Space) || (key == Qt::Key_Select);
    }
    if (!toggle) {
      return false;
    }
    const bool included = (index.data(Qt::CheckStateRole).toInt() == Qt::Checked);
    return model->setData(index, included ? Qt::Unchecked : Qt::Checked, Qt::CheckStateRole);
  }

  /** The file name shows its full text, the rest of the tile the information about the page. */
  bool helpEvent(QHelpEvent* event,
                 QAbstractItemView* view,
                 const QStyleOptionViewItem& option,
                 const QModelIndex& index) override {
    if (event->type() != QEvent::ToolTip) {
      return QStyledItemDelegate::helpEvent(event, view, option, index);
    }
    // The list may hand over the nearest tile for the background beside it; only the tile itself has a tooltip.
    // REVIEW-NOTE (2026-11): no regression test -> qt_tests doesn't build the views of the app. Fixed after a hand
    // test.
    if (!option.rect.contains(event->pos())) {
      QToolTip::hideText();
      return true;
    }
    const QString tip = nameRect(option).contains(event->pos())
                            ? index.data(Qt::DisplayRole).toString()
                            : m_view->pageToolTip(index.data(ENTRY_INDEX_ROLE).toInt());
    if (tip.isEmpty()) {
      QToolTip::hideText();
    } else {
      // Bound to the tile, so that the same text over another tile moves along.
      QToolTip::showText(event->globalPos(), Application::wrapToolTip(tip), view->viewport(), option.rect);
    }
    return true;
  }

 private:
  static QRect nameRect(const QStyleOptionViewItem& option) {
    const int top
        = option.rect.top() + TILE_MARGIN + headerHeight(option) + TILE_MARGIN + THUMBNAIL_SIZE.height() + TILE_MARGIN;
    return QRect(option.rect.left() + TILE_MARGIN, top, option.rect.width() - 2 * TILE_MARGIN,
                 option.fontMetrics.height());
  }

  static int headerHeight(const QStyleOptionViewItem& option) {
    const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
    return std::max(option.fontMetrics.height() + 4, style->pixelMetric(QStyle::PM_IndicatorHeight));
  }

  static QRect checkRect(const QStyleOptionViewItem& option) {
    const QStyle* style = option.widget ? option.widget->style() : QApplication::style();
    const QSize size(style->pixelMetric(QStyle::PM_IndicatorWidth), style->pixelMetric(QStyle::PM_IndicatorHeight));
    const int top = option.rect.top() + TILE_MARGIN + (headerHeight(option) - size.height()) / 2;
    return QRect(QPoint(option.rect.left() + TILE_MARGIN, top), size);
  }

  const ExportView* m_view;
};

class ExportView::ThumbnailHandler : public ThumbnailPixmapCache::CompletionHandler {
 public:
  ThumbnailHandler(ExportView* view, const int generation, const int entryIndex)
      : m_view(view), m_generation(generation), m_entryIndex(entryIndex) {}

  void operator()(const ThumbnailLoadResult& result) override {
    m_view->thumbnailLoaded(m_generation, m_entryIndex, result);
  }

 private:
  ExportView* m_view;
  int m_generation;
  int m_entryIndex;
};


ExportView::ExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
                       std::shared_ptr<ProcessingOptions> processingOptions,
                       QWidget* parent)
    : QWidget(parent), m_thumbnailCache(std::move(thumbnailCache)), m_processingOptions(std::move(processingOptions)) {
  auto* mainLayout = new QVBoxLayout(this);

  // Page list.
  auto* pagesGroup = new QGroupBox(tr("Pages"));
  auto* pagesLayout = new QVBoxLayout(pagesGroup);
  // Tiles from left to right, then on the next row, like reading. The list mode (rather than
  // the icon mode) keeps drag and drop a change of the order.
  m_pageList = new QListWidget;
  m_pageList->setViewMode(QListView::ListMode);
  m_pageList->setFlow(QListView::LeftToRight);
  m_pageList->setWrapping(true);
  m_pageList->setResizeMode(QListView::Adjust);
  m_pageList->setUniformItemSizes(true);
  m_pageList->setSpacing(4);
  m_pageList->setIconSize(THUMBNAIL_SIZE);
  m_pageList->setItemDelegate(new PageTileDelegate(this, m_pageList));
  m_pageList->setSelectionMode(QAbstractItemView::ExtendedSelection);
  m_pageList->setDragDropMode(QAbstractItemView::InternalMove);
  m_pageList->setDefaultDropAction(Qt::MoveAction);
  pagesLayout->addWidget(m_pageList, 1);

  m_selectionLabel = new QLabel;
  pagesLayout->addWidget(m_selectionLabel);

  auto* listButtons = new QHBoxLayout;
  m_allButton = new QPushButton(tr("All"));
  m_noneButton = new QPushButton(tr("None"));
  m_forwardButton = new QPushButton(tr("Move forward"));
  m_backButton = new QPushButton(tr("Move back"));
  m_allButton->setToolTip(tr("Include all output pages in the export."));
  m_noneButton->setToolTip(tr("Exclude all pages from the export."));
  m_forwardButton->setToolTip(
      tr("Move the selected pages one place towards the start of the export. Pages can also be moved with drag "
         "and drop."));
  m_backButton->setToolTip(
      tr("Move the selected pages one place towards the end of the export. Pages can also be moved with drag and "
         "drop."));
  // Shown only while pages are missing or changed since the output, next to the buttons that choose the pages.
  m_outputMissingButton = new QPushButton(tr("Output missing and changed pages"));
  m_outputMissingButton->setToolTip(
      tr("Process the pages not output yet or changed since the output in the Output step, then come back here."));
  listButtons->addWidget(m_allButton);
  listButtons->addWidget(m_noneButton);
  listButtons->addSpacing(12);
  listButtons->addWidget(m_outputMissingButton);
  listButtons->addStretch(1);
  listButtons->addWidget(m_forwardButton);
  listButtons->addWidget(m_backButton);
  pagesLayout->addLayout(listButtons);
  mainLayout->addWidget(pagesGroup, 1);

  // In a group like the pages: on the left where the files go (a folder and a name, to which each
  // format adds its extension) and the action, sharing one right edge; on the right what the export
  // will create, is doing or has done. The left side doesn't take the whole width of a wide window;
  // a path and a name need far less.
  auto* exportGroup = new QGroupBox(tr("Export Files"));
  auto* targetRow = new QHBoxLayout(exportGroup);
  auto* fields = new QWidget;
  fields->setMaximumWidth(640);
  auto* fieldsLayout = new QGridLayout(fields);
  fieldsLayout->setContentsMargins(0, 0, 0, 0);
  fieldsLayout->addWidget(new QLabel(tr("Folder:")), 0, 0);
  m_folderEdit = new QLineEdit;
  fieldsLayout->addWidget(m_folderEdit, 0, 1);
  m_browseButton = new QPushButton(tr("Browse ..."));
  fieldsLayout->addWidget(m_browseButton, 0, 2);
  fieldsLayout->addWidget(new QLabel(tr("File name:")), 1, 0);
  m_nameEdit = new QLineEdit;
  m_nameEdit->setToolTip(tr("The name of the exported files, without extension. Each format adds its own."));
  fieldsLayout->addWidget(m_nameEdit, 1, 1);
  fieldsLayout->setColumnStretch(1, 1);

  // The progress and the button that starts it, below the fields.
  auto* buttons = new QHBoxLayout;
  m_progressBar = new QProgressBar;
  m_progressBar->setRange(0, 1);
  m_progressBar->setValue(0);
  m_progressBar->setFormat("%v / %m");
  buttons->addWidget(m_progressBar, 1);
  buttons->addSpacing(12);
  m_cancelButton = new QPushButton(tr("Cancel"));
  m_cancelButton->setVisible(false);
  m_createButton = new AccentFrameButton(tr("Export"));
  m_createButton->setDefault(true);
  // While an export runs, Cancel takes the place of Export, so the progress bar keeps its length.
  const int buttonWidth = std::max(m_cancelButton->sizeHint().width(), m_createButton->sizeHint().width());
  m_cancelButton->setFixedWidth(buttonWidth);
  m_createButton->setFixedWidth(buttonWidth);
  buttons->addWidget(m_cancelButton);
  buttons->addWidget(m_createButton);
  fieldsLayout->addLayout(buttons, 2, 0, 1, 3);

  targetRow->addWidget(fields, 1);
  targetRow->addSpacing(18);
  m_statusLabel = new QLabel;
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
  // After an export it holds links to the files, which the keyboard reaches as well.
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::LinksAccessibleByMouse
                                         | Qt::LinksAccessibleByKeyboard);
  targetRow->addWidget(m_statusLabel, 1);
  mainLayout->addWidget(exportGroup);
  connect(m_statusLabel, &QLabel::linkActivated, this,
          [](const QString& link) { QDesktopServices::openUrl(QUrl(link)); });
  connect(m_statusLabel, &QLabel::linkHovered, this, [](const QString& link) {
    QToolTip::showText(QCursor::pos(), link.isEmpty() ? QString() : QDir::toNativeSeparators(QUrl(link).toLocalFile()));
  });

  m_optionsWidget.reset(createOptionsWidget());

  connect(m_pageList, &QListWidget::itemChanged, this, &ExportView::updatePositions);
  connect(m_pageList, &QListWidget::itemChanged, this, &ExportView::updateControls);
  connect(m_pageList, &QListWidget::itemSelectionChanged, this, &ExportView::updateControls);
  // Drag and drop as well as the buttons move items.
  connect(m_pageList->model(), &QAbstractItemModel::rowsMoved, this, &ExportView::updatePositions);
  // A new order makes the result of the last export outdated (see showStatus()).
  connect(m_pageList->model(), &QAbstractItemModel::rowsMoved, this, &ExportView::updateControls);
  connect(m_pageList->model(), &QAbstractItemModel::rowsInserted, this, &ExportView::updatePositions);
  connect(m_pageList->model(), &QAbstractItemModel::rowsRemoved, this, &ExportView::updatePositions);
  connect(m_allButton, &QPushButton::clicked, this, [this]() { setAllChecked(Qt::Checked); });
  connect(m_noneButton, &QPushButton::clicked, this, [this]() { setAllChecked(Qt::Unchecked); });
  connect(m_outputMissingButton, &QPushButton::clicked, this, &ExportView::outputMissingPagesRequested);
  connect(m_forwardButton, &QPushButton::clicked, this, &ExportView::moveForward);
  connect(m_backButton, &QPushButton::clicked, this, &ExportView::moveBack);
  for (QLineEdit* edit : {m_folderEdit, m_nameEdit}) {
    connect(edit, &QLineEdit::textEdited, this, [this]() {
      m_fileChosen = true;
      showPreview();
    });
  }
  connect(m_browseButton, &QPushButton::clicked, this, &ExportView::browse);
  connect(m_createButton, &QPushButton::clicked, this, &ExportView::startExport);
  connect(m_cancelButton, &QPushButton::clicked, this, &ExportView::cancelExport);

  updateFormatDependents();
}

ExportView::~ExportView() {
  if (m_thread) {
    m_job->cancel();
    m_thread->wait();
    delete m_thread;
  }
}

QWidget* ExportView::createOptionsWidget() {
  auto* widget = new QWidget;
  // The default margins, as around the options of the other steps.
  auto* layout = new QVBoxLayout(widget);

  // Collapsible like the panels of the other steps; the object name keeps its collapsed state.
  // First what is created, then how the text is recognized, then the panels of the PDF alone.
  auto* formatsGroup = new CollapsibleGroupBox(tr("Formats"));
  formatsGroup->setObjectName("exportFormatsPanel");
  auto* formatsLayout = new QVBoxLayout(formatsGroup);
  m_formatsPanel = new ExportFormatsPanel;
  m_formatsPanel->setOptions(*m_processingOptions);
  formatsLayout->addWidget(m_formatsPanel);
  layout->addWidget(formatsGroup);
  connect(m_formatsPanel, &ExportFormatsPanel::optionsChanged, this, &ExportView::saveSettings);

  auto* ocrGroup = new CollapsibleGroupBox(tr("Text Recognition (OCR)"));
  ocrGroup->setObjectName("pdfOcrPanel");
  auto* ocrGroupLayout = new QVBoxLayout(ocrGroup);
  m_ocrPanel = new OcrPanel;
  m_ocrPanel->setOptions(m_processingOptions->ocr);
  ocrGroupLayout->addWidget(m_ocrPanel);
  layout->addWidget(ocrGroup);
  m_ocrGroup = ocrGroup;
  connect(m_ocrPanel, &OcrPanel::optionsChanged, this, &ExportView::saveSettings);

  auto* compressionGroup = new CollapsibleGroupBox(tr("PDF Compression"));
  compressionGroup->setObjectName("pdfCompressionPanel");
  auto* compressionLayout = new QVBoxLayout(compressionGroup);
  m_compressionPanel = new PdfCompressionPanel;
  m_compressionPanel->setOptions(m_processingOptions->pdf);
  compressionLayout->addWidget(m_compressionPanel);
  m_colorCompression = m_compressionPanel->colorCompressionBox();
  m_colorQuality = m_compressionPanel->colorQualityBox();
  m_paletteCompression = m_compressionPanel->paletteCompressionBox();
  m_paletteQuality = m_compressionPanel->paletteQualityBox();
  m_bitonalCompression = m_compressionPanel->bitonalCompressionBox();
  layout->addWidget(compressionGroup);
  m_compressionGroup = compressionGroup;
  connect(m_compressionPanel, &PdfCompressionPanel::compressionChanged, this, &ExportView::compressionChanged);

  // Quality: the compression of the ticked pages, in the output folder and in the PDF.
  auto* qualityGroup = new CollapsibleGroupBox(tr("Quality"));
  qualityGroup->setObjectName("pdfQualityPanel");
  m_qualityLayout = new QFormLayout(qualityGroup);
  m_qualityLayout->setRowWrapPolicy(QFormLayout::DontWrapRows);
  // Labels stay at the top of values that wrap.
  m_qualityLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignTop);
  const QString kindLabels[3] = {tr("Color / grayscale:"), tr("Posterized:"), tr("Black and white:")};
  auto addPart = [this, &kindLabels](const QString& heading, const QString& toolTip, const bool first, QLabel** rows,
                                     QLabel** noPages, QLabel** warning) {
    auto* sectionHeading = new SectionHeading(heading);
    sectionHeading->setToolTip(toolTip);
    sectionHeading->setFirst(first);
    m_qualityLayout->addRow(sectionHeading);
    *noPages = new QLabel(tr("No pages selected."));
    m_qualityLayout->addRow(*noPages);
    for (int i = 0; i < 3; ++i) {
      rows[i] = new QLabel;
      rows[i]->setWordWrap(true);
      auto* label = new QLabel(kindLabels[i]);
      label->setToolTip(toolTip);
      rows[i]->setToolTip(toolTip);
      m_qualityLayout->addRow(label, rows[i]);
    }
    *warning = new QLabel;
    (*warning)->setWordWrap(true);
    m_qualityLayout->addRow(*warning);
  };
  addPart(tr("Source Files (TIFF)"),
          tr("The compression of the TIFF files in the output folder that the selected pages are made from, as set "
             "in the TIFF Compression panel of the Output step when the pages were processed. The numbers count the "
             "pages; pages with split output count with their picture and with their text."),
          true, m_tiffQuality, &m_tiffNoPages, &m_tiffWarning);
  addPart(QStringLiteral("PDF"),
          tr("How the selected pages are compressed in the PDF, as set in the PDF Compression panel. The numbers "
             "count the pages; pages with split output count with their picture and with their text."),
          false, m_pdfQuality, &m_pdfNoPages, &m_pdfWarning);
  layout->addWidget(qualityGroup);
  m_qualityGroup = qualityGroup;

  layout->addStretch(1);
  return widget;
}  // ExportView::createOptionsWidget

QWidget* ExportView::optionsWidget() const {
  return m_optionsWidget.get();
}

void ExportView::setPages(std::vector<Entry> entries) {
  if (m_thread) {
    return;
  }

  // Thumbnails still being loaded belong to the previous pages.
  ++m_generation;
  m_entries = std::move(entries);
  m_thumbnailHandlers.clear();
  m_thumbnailHandlers.resize(m_entries.size());
  m_thumbnailShown.assign(m_entries.size(), false);

  // The order and the ticks shown so far, and the thumbnails, which are shown again until
  // they have been reloaded. The output file identifies a page.
  std::vector<PdfPageOrder::Item> previous;
  QHash<QString, QIcon> previousIcons;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    const QString key = item->data(KEY_ROLE).toString();
    previous.push_back(PdfPageOrder::Item{key, item->checkState() == Qt::Checked});
    if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
      previousIcons.insert(key, item->icon());
    }
  }
  std::vector<QString> projectOrder;
  QHash<QString, int> entryIndexes;
  for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
    const QString key = QDir::cleanPath(m_entries[i].page.mainFile());
    projectOrder.push_back(key);
    entryIndexes.insert(key, i);
  }
  const std::vector<PdfPageOrder::Item> order = PdfPageOrder::merge(previous, projectOrder);

  const int scrollPos = m_pageList->verticalScrollBar()->value();
  m_filling = true;
  {
    const QSignalBlocker blocker(m_pageList);
    m_pageList->clear();
    for (const PdfPageOrder::Item& orderItem : order) {
      const int i = entryIndexes.value(orderItem.key);
      const ExportPage& page = m_entries[i].page;
      auto* item = new QListWidgetItem(m_pageList);
      item->setData(ENTRY_INDEX_ROLE, i);
      item->setData(KEY_ROLE, orderItem.key);
      item->setText(m_entries[i].label);
      // Kept for pages that aren't output yet, for when they are.
      item->setCheckState(orderItem.checked ? Qt::Checked : Qt::Unchecked);
      if (page.kind() == ExportPage::MISSING) {
        // Can't be ticked or selected; the placeholder shows the hourglass of pages not processed yet.
        item->setFlags(Qt::ItemIsEnabled);
        item->setIcon(makeIcon(QPixmap(), true));
      } else {
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled);
        item->setIcon(previousIcons.value(orderItem.key, makeIcon(QPixmap(), false, page.isOutdated())));
      }
    }
  }
  m_filling = false;
  updatePageSigns();
  updatePositions();
  QTimer::singleShot(0, this, [this, scrollPos]() { m_pageList->verticalScrollBar()->setValue(scrollPos); });

  for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
    if (m_entries[i].page.kind() != ExportPage::MISSING) {
      requestThumbnail(m_generation, i);
    }
  }
  updateControls();
}  // ExportView::setPages

void ExportView::setDefaultBase(const QString& base) {
  if (!m_fileChosen) {
    const QFileInfo info(base);
    m_folderEdit->setText(QDir::toNativeSeparators(info.path()));
    m_nameEdit->setText(info.fileName());
    updateControls();
  }
}

bool ExportView::isRunning() const {
  return m_thread != nullptr;
}

std::set<PageId> ExportView::pagesToOutput() const {
  std::vector<ExportPage> pages;
  pages.reserve(m_entries.size());
  for (const Entry& entry : m_entries) {
    pages.push_back(entry.page);
  }
  return ExportPage::pagesToOutput(pages);
}

void ExportView::cancelAndWait() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  m_thread->wait();
  exportFinished();
}

void ExportView::setAllChecked(const Qt::CheckState state) {
  {
    // Numbered once at the end rather than for every page.
    const QSignalBlocker blocker(m_pageList);
    for (int row = 0; row < m_pageList->count(); ++row) {
      QListWidgetItem* item = m_pageList->item(row);
      if (item->flags().testFlag(Qt::ItemIsUserCheckable)) {
        item->setCheckState(state);
      }
    }
  }
  updatePositions();
  updateControls();
}

void ExportView::updatePositions() {
  if (m_filling) {
    return;
  }
  // Setting the positions isn't a change by the user.
  const QSignalBlocker blocker(m_pageList);
  int position = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    const bool included = item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked);
    const int newPosition = included ? ++position : 0;
    if (item->data(POSITION_ROLE).toInt() != newPosition) {
      item->setData(POSITION_ROLE, newPosition);
    }
  }
}

void ExportView::moveForward() {
  std::vector<int> rows;
  for (QListWidgetItem* item : m_pageList->selectedItems()) {
    rows.push_back(m_pageList->row(item));
  }
  std::sort(rows.begin(), rows.end());
  if (rows.empty() || (rows.front() == 0)) {
    return;
  }
  for (const int row : rows) {
    QListWidgetItem* item = m_pageList->takeItem(row);
    m_pageList->insertItem(row - 1, item);
  }
  m_pageList->clearSelection();
  for (const int row : rows) {
    m_pageList->item(row - 1)->setSelected(true);
  }
  m_pageList->scrollToItem(m_pageList->item(rows.front() - 1));
}

void ExportView::moveBack() {
  std::vector<int> rows;
  for (QListWidgetItem* item : m_pageList->selectedItems()) {
    rows.push_back(m_pageList->row(item));
  }
  std::sort(rows.begin(), rows.end(), std::greater<>());
  if (rows.empty() || (rows.front() == m_pageList->count() - 1)) {
    return;
  }
  for (const int row : rows) {
    QListWidgetItem* item = m_pageList->takeItem(row);
    m_pageList->insertItem(row + 1, item);
  }
  m_pageList->clearSelection();
  for (const int row : rows) {
    m_pageList->item(row + 1)->setSelected(true);
  }
  m_pageList->scrollToItem(m_pageList->item(rows.front() + 1));
}

void ExportView::browse() {
  const QString folder = QFileDialog::getExistingDirectory(this, tr("Choose Export Folder"), m_folderEdit->text());
  if (folder.isEmpty()) {
    return;
  }
  m_folderEdit->setText(QDir::toNativeSeparators(folder));
  m_fileChosen = true;
  showPreview();
}

QString ExportView::exportBase(QString* problem) const {
  const QString folder = QDir::fromNativeSeparators(m_folderEdit->text().trimmed());
  // An extension of a format, as someone may type it, is no part of the name.
  const QString name = ExportPaths::nameFromInput(m_nameEdit->text());
  if (folder.isEmpty()) {
    *problem = tr("Choose the folder for the export.");
    return QString();
  }
  if (QDir::isRelativePath(folder)) {
    *problem = tr("Enter the complete path of the folder.");
    return QString();
  }
  // Also for the existing folder: Windows calls "D:/Books." existing if "D:/Books" exists.
  QString reason = path_names::invalidNameReason(name);
  if (reason.isEmpty()) {
    reason = path_names::invalidNewPathReason(folder);
  }
  if (!reason.isEmpty()) {
    *problem = reason;
    return QString();
  }
  if (!QFileInfo(folder).isDir()) {
    *problem = tr("The folder %1 doesn't exist.").arg(QDir::toNativeSeparators(folder));
    return QString();
  }
  return QDir(QDir::cleanPath(folder)).filePath(name);
}

QStringList ExportView::existingOutputs(const ExportPaths& paths, const ProcessingOptions::Formats& formats) const {
  QStringList existing;
  for (const auto& [chosen, file] :
       {std::pair(formats.pdf, paths.pdf), std::pair(formats.txt, paths.txt), std::pair(formats.hocr, paths.hocr)}) {
    if (chosen && QFileInfo::exists(file)) {
      existing.push_back(QFileInfo(file).fileName());
    }
  }
  if (formats.alto && QFileInfo::exists(paths.altoDir)) {
    const int count = static_cast<int>(QDir(paths.altoDir).entryList(QDir::Files | QDir::Hidden).size());
    existing.push_back(tr("%1 (folder with %n file(s))", nullptr, count).arg(QFileInfo(paths.altoDir).fileName()));
  }
  return existing;
}

void ExportView::startExport() {
  if (m_thread) {
    return;
  }
  const QString title = tr("Export");

  QString problem;
  const QString base = exportBase(&problem);
  if (base.isEmpty()) {
    QMessageBox::warning(this, title, problem);
    return;
  }
  const ExportPaths paths = ExportPaths::fromBase(base);
  const ProcessingOptions::Formats formats = m_processingOptions->formats;
  if (!formats.any()) {
    QMessageBox::warning(this, title, tr("Tick at least one format."));
    return;
  }

  // Asked first: whoever goes back to output the pages exports later.
  int outdated = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)
        && m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page.isOutdated()) {
      ++outdated;
    }
  }
  if (outdated > 0) {
    QMessageBox box(QMessageBox::Question, title,
                    tr("%n selected page(s) changed since the output. The export would show the state of the last "
                       "output.\n\nDo you want to export anyway? To include the changes, choose \"Back\" "
                       "and then \"Output missing and changed pages\".",
                       nullptr, outdated));
    QPushButton* createButton = box.addButton(tr("Export anyway"), QMessageBox::AcceptRole);
    QPushButton* backButton = box.addButton(tr("Back"), QMessageBox::RejectRole);
    box.setDefaultButton(backButton);
    box.setEscapeButton(backButton);
    box.exec();
    if (box.clickedButton() != createButton) {
      return;
    }
  }

  // Asked each time, even about files this view created: they may have been changed outside the
  // program since.
  const QStringList existing = existingOutputs(paths, formats);
  if (!existing.isEmpty()) {
    const QMessageBox::StandardButton answer
        = QMessageBox::question(this, title,
                                tr("These already exist in %1:\n\n%2\n\nDo you want to replace them?")
                                    .arg(QDir::toNativeSeparators(QFileInfo(base).path()), existing.join('\n')),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }

  std::vector<ExportPage> pages;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      pages.push_back(m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page);
    }
  }
  if (pages.empty()) {
    QMessageBox::information(this, title, tr("Please tick the pages to include in the export."));
    return;
  }

  ExportOptions options = ExportOptions::fromProject(*m_processingOptions);
  options.threadCount = ExportJob::defaultThreadCount();

  if (m_processingOptions->needsTextRecognition()) {
    if (!m_ocrPanel->missingLanguages().isEmpty()) {
      const QMessageBox::StandardButton answer = QMessageBox::question(
          this, title,
          tr("The languages %1 of the project aren't installed on this computer. Download them now?\n\nWithout "
             "them, the text is only recognized with the other ticked languages.")
              .arg(m_ocrPanel->missingLanguageNames()),
          QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
      if (answer == QMessageBox::Cancel) {
        return;
      }
      if (answer == QMessageBox::Yes) {
        m_ocrPanel->downloadMissingLanguages();
        // The download may have been canceled or failed.
        if (!m_ocrPanel->missingLanguages().isEmpty()) {
          const QMessageBox::StandardButton goOn = QMessageBox::question(
              this, title,
              tr("The languages %1 still aren't installed. Recognize the text with the other ticked languages "
                 "only?")
                  .arg(m_ocrPanel->missingLanguageNames()),
              QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Yes);
          if (goOn != QMessageBox::Yes) {
            return;
          }
        }
      }
    }
    const QStringList languages = m_ocrPanel->checkedLanguages();
    // Only copying the language files into one folder takes noticeable time.
    const bool copying = !languages.isEmpty() && OcrLanguages::commonDir(languages).isEmpty();
    if (copying) {
      showStatus(tr("Copying language files ..."));
      QApplication::setOverrideCursor(Qt::WaitCursor);
    }
    QString error;
    const ExportOptions::OcrSetup setup = options.setUpOcr(languages, m_ocrPanel->pageLayout(), &error);
    if (copying) {
      QApplication::restoreOverrideCursor();
      showPreview();
    }
    if (setup == ExportOptions::OcrSetup::NO_LANGUAGE) {
      // Missing languages that were not downloaded leave none.
      QMessageBox::warning(this, title, tr("Please tick at least one installed language for the text recognition."));
      return;
    }
    if (setup == ExportOptions::OcrSetup::NOT_GATHERED) {
      QMessageBox::warning(this, title,
                           tr("The files of the selected languages are in different folders, and they could "
                              "not be copied into one folder, which text recognition needs.")
                               + QStringLiteral("\n\n") + error);
      return;
    }
  }

  const int pageCount = static_cast<int>(pages.size());
  m_job = std::make_unique<ExportJob>(std::move(pages), paths, options);
  connect(m_job.get(), &ExportJob::progress, this, &ExportView::exportProgress, Qt::QueuedConnection);
  ExportJob* job = m_job.get();
  m_thread = QThread::create([job]() { job->run(); });
  connect(m_thread, &QThread::finished, this, &ExportView::exportFinished);

  m_runningPaths = paths;
  m_runningFormats = formats;
  m_progressBar->setRange(0, pageCount);
  m_progressBar->setValue(0);
  showStatus(options.runsTextRecognition() ? tr("Recognizing the text and exporting ...") : tr("Exporting ..."));
  setRunning(true);
  m_thread->start();
}  // ExportView::startExport

void ExportView::cancelExport() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  showStatus(tr("Canceling ..."));
  m_cancelButton->setEnabled(false);
}

void ExportView::exportProgress(const int pagesDone, const int pagesTotal) {
  if (!m_thread) {
    return;
  }
  m_progressBar->setRange(0, pagesTotal);
  m_progressBar->setValue(pagesDone);
}

void ExportView::exportFinished() {
  if (!m_thread) {
    return;
  }
  m_thread->wait();
  delete m_thread;
  m_thread = nullptr;
  const std::unique_ptr<ExportJob> job = std::move(m_job);
  setRunning(false);

  if (job->succeeded()) {
    showCreatedFiles();
  } else if (job->wasCancelled()) {
    m_progressBar->setValue(0);
    showStatus(tr("Canceled. Nothing was exported."));
  } else {
    m_progressBar->setValue(0);
    showStatus(tr("The export failed."));
    QStringList errors = job->errors();
    const int maxShown = 20;
    if (errors.size() > maxShown) {
      const int hidden = static_cast<int>(errors.size()) - maxShown;
      errors = errors.mid(0, maxShown);
      errors.push_back(tr("... and %1 more.").arg(hidden));
    }
    QMessageBox::warning(this, tr("Export"), tr("The export failed.") + QStringLiteral("\n\n") + errors.join('\n'));
  }
}  // ExportView::exportFinished

void ExportView::showCreatedFiles() {
  // Opening a link opens the file with its program, or the folder in the file manager.
  const auto link = [](const QString& path, const QString& text) {
    return QStringLiteral("<a href=\"%1\">%2</a>")
        .arg(QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded).toHtmlEscaped(), text.toHtmlEscaped());
  };
  const auto fileLink = [&link](const QString& path) { return link(path, QFileInfo(path).fileName()); };

  const auto fileWithSize = [&fileLink](const QString& path) {
    return fileLink(path) + QStringLiteral(" (") + QLocale().formattedDataSize(QFileInfo(path).size()).toHtmlEscaped()
           + QLatin1Char(')');
  };

  QStringList created;
  for (const auto& [chosen, file] :
       {std::pair(m_runningFormats.pdf, m_runningPaths.pdf), std::pair(m_runningFormats.txt, m_runningPaths.txt),
        std::pair(m_runningFormats.hocr, m_runningPaths.hocr)}) {
    if (chosen) {
      created.push_back(fileWithSize(file));
    }
  }
  if (m_runningFormats.alto) {
    const int count = static_cast<int>(QDir(m_runningPaths.altoDir).entryList(QDir::Files).size());
    created.push_back(fileLink(m_runningPaths.altoDir) + QStringLiteral(" (")
                      + tr("%n file(s)", nullptr, count).toHtmlEscaped() + QLatin1Char(')'));
  }
  // The folder once at the start, as all files are in it.
  const QString folder = QFileInfo(m_runningPaths.pdf).absolutePath();
  showStatus(bulletList(tr("Created in %1:").arg(link(folder, QDir::toNativeSeparators(folder))), created),
             Qt::RichText);
}

QString ExportView::bulletList(const QString& heading, const QStringList& items) {
  QString html = heading;
  for (const QString& item : items) {
    html += QStringLiteral("<br>&bull;&nbsp;") + item;
  }
  return html;
}

void ExportView::showStatus(const QString& text, const Qt::TextFormat format) {
  m_showingStatus = true;
  // The result stands until what it is about changes.
  m_statusPages = checkedEntries();
  m_statusLabel->setTextFormat(format);
  m_statusLabel->setText(text);
}

void ExportView::showPreview() {
  m_showingStatus = false;
  updateControls();
}

void ExportView::showPreviewText(const QString& text, const Qt::TextFormat format) {
  m_statusLabel->setTextFormat(format);
  m_statusLabel->setText(text);
}

std::vector<int> ExportView::checkedEntries() const {
  std::vector<int> entries;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      entries.push_back(item->data(ENTRY_INDEX_ROLE).toInt());
    }
  }
  return entries;
}

void ExportView::updateControls() {
  const bool running = (m_thread != nullptr);
  int available = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    if (m_pageList->item(row)->flags().testFlag(Qt::ItemIsUserCheckable)) {
      ++available;
    }
  }
  const int checked = checkedCount();
  // Each statement is a sentence of its own.
  m_selectionLabel->setText(tr("%1 of %2 output pages selected.").arg(checked).arg(available));
  const int missing = m_pageList->count() - available;
  if (missing > 0) {
    m_selectionLabel->setText(m_selectionLabel->text() + QLatin1Char(' ')
                              + tr("%n page(s) not output yet.", nullptr, missing));
  }
  const int outdated = static_cast<int>(
      std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& entry) { return entry.page.isOutdated(); }));
  if (outdated > 0) {
    m_selectionLabel->setText(m_selectionLabel->text() + QLatin1Char(' ')
                              + tr("%n page(s) changed since the output.", nullptr, outdated));
  }

  m_outputMissingButton->setVisible((missing > 0) || (outdated > 0));
  m_outputMissingButton->setEnabled(!running);

  const bool hasSelection = !m_pageList->selectedItems().isEmpty();
  m_forwardButton->setEnabled(!running && hasSelection);
  m_backButton->setEnabled(!running && hasSelection);

  // A message or the result of the last export stands until the ticked pages or their order change;
  // the fields and the options switch back themselves (showPreview()).
  if (m_showingStatus && !running && (checkedEntries() != m_statusPages)) {
    m_showingStatus = false;
  }

  // What the export creates, or what is wrong with the folder or the name, already while typing.
  const ProcessingOptions::Formats& formats = m_processingOptions->formats;
  QString targetProblem;
  const QString base = exportBase(&targetProblem);
  if (m_showingStatus) {
    // Left as it is.
  } else if (!formats.any()) {
    showPreviewText(QString());
  } else if (base.isEmpty()) {
    showPreviewText(warningSign() + ' ' + targetProblem);
  } else {
    const ExportPaths paths = ExportPaths::fromBase(base);
    QStringList names;
    for (const auto& [chosen, file] :
         {std::pair(formats.pdf, paths.pdf), std::pair(formats.txt, paths.txt), std::pair(formats.hocr, paths.hocr)}) {
      if (chosen) {
        names.push_back(QFileInfo(file).fileName().toHtmlEscaped());
      }
    }
    if (formats.alto) {
      names.push_back((QFileInfo(paths.altoDir).fileName() + QDir::separator()).toHtmlEscaped());
    }
    // The folder stands in the field beside it; the result names it, as the field may change.
    showPreviewText(bulletList(tr("Will be created:"), names), Qt::RichText);
  }

  // The button tells why it can't be used.
  QString blocked;
  if (!formats.any()) {
    blocked = tr("Tick at least one format.");
  } else if (m_processingOptions->needsTextRecognition() && !m_ocrPanel->hasTickedLanguage()) {
    blocked = tr("Tick at least one language for the text recognition.");
  } else if (base.isEmpty()) {
    blocked = targetProblem;
  } else if (checked == 0) {
    blocked = tr("Tick the pages to export.");
  }
  m_createButton->setEnabled(!running && blocked.isEmpty());
  m_createButton->setToolTip(blocked.isEmpty() ? tr("Export the ticked pages in the chosen formats.") : blocked);
  updateQualityPanel();
}

void ExportView::updateFormatDependents() {
  const bool needsText = m_processingOptions->needsTextRecognition();
  m_ocrGroup->setEnabled(needsText);
  m_ocrPanel->setTextNeeded(needsText);
  m_compressionGroup->setEnabled(m_processingOptions->formats.pdf);
  m_qualityGroup->setEnabled(m_processingOptions->formats.pdf);
  updateControls();
}

void ExportView::saveSettings() {
  // All of them belong to the project.
  const ProcessingOptions::Formats formatsBefore = m_processingOptions->formats;
  const ProcessingOptions::Pdf pdfBefore = m_processingOptions->pdf;
  const ProcessingOptions::Ocr ocrBefore = m_processingOptions->ocr;
  m_compressionPanel->storeOptions(m_processingOptions->pdf);
  m_formatsPanel->storeOptions(*m_processingOptions);
  m_ocrPanel->storeOptions(m_processingOptions->ocr);
  if ((m_processingOptions->formats != formatsBefore) || (m_processingOptions->pdf != pdfBefore)
      || (m_processingOptions->ocr != ocrBefore)) {
    // The project changed, so it gets saved automatically like other changes.
    emit projectOptionsChanged();
    // The result of the last export no longer matches what an export would create now.
    m_showingStatus = false;
  }
  updateFormatDependents();
}

void ExportView::requestThumbnail(const int generation, const int entryIndex) {
  if (!m_thumbnailCache || (generation != m_generation) || (entryIndex >= static_cast<int>(m_entries.size()))) {
    return;
  }
  auto handler = std::make_shared<ThumbnailHandler>(this, generation, entryIndex);
  QPixmap pixmap;
  const ThumbnailPixmapCache::Status status
      = m_thumbnailCache->loadRequest(ImageId(m_entries[entryIndex].page.mainFile()), pixmap, handler);
  if (status == ThumbnailPixmapCache::LOADED) {
    showThumbnail(entryIndex, pixmap);
    m_thumbnailHandlers[entryIndex].reset();
  } else if (status == ThumbnailPixmapCache::QUEUED) {
    // The cache only keeps a weak reference.
    m_thumbnailHandlers[entryIndex] = handler;
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

void ExportView::thumbnailLoaded(const int generation, const int entryIndex, const ThumbnailLoadResult& result) {
  if (generation != m_generation) {
    return;
  }
  if (result.status() == ThumbnailLoadResult::LOADED) {
    showThumbnail(entryIndex, result.pixmap());
    m_thumbnailHandlers[entryIndex].reset();
  } else if (result.status() == ThumbnailLoadResult::REQUEST_EXPIRED) {
    // The cache serves the newest requests first and drops old ones. Ask again.
    QTimer::singleShot(0, this, [this, generation, entryIndex]() { requestThumbnail(generation, entryIndex); });
  } else {
    m_thumbnailHandlers[entryIndex].reset();
  }
}

QListWidgetItem* ExportView::itemForEntry(const int entryIndex) const {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->data(ENTRY_INDEX_ROLE).toInt() == entryIndex) {
      return item;
    }
  }
  return nullptr;
}

void ExportView::showThumbnail(const int entryIndex, const QPixmap& thumbnail) {
  if (QListWidgetItem* item = itemForEntry(entryIndex)) {
    item->setIcon(makeIcon(thumbnail, false, m_entries[entryIndex].page.isOutdated()));
  }
  m_thumbnailShown[entryIndex] = true;

  if (!m_placeholderAspectKnown && !thumbnail.isNull() && (thumbnail.height() > 0)) {
    // The pages of a project usually have the same proportions; draw the placeholders like them.
    m_placeholderAspectKnown = true;
    m_placeholderAspect = double(thumbnail.width()) / thumbnail.height();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
      if (!m_thumbnailShown[i]) {
        if (QListWidgetItem* item = itemForEntry(i)) {
          const ExportPage& page = m_entries[i].page;
          item->setIcon(makeIcon(QPixmap(), page.kind() == ExportPage::MISSING, page.isOutdated()));
        }
      }
    }
  }
}

QIcon ExportView::makeIcon(const QPixmap& thumbnail, const bool missing, const bool outdated) const {
  // All icons have the same size, so that the texts line up.
  const QSize size = m_pageList->iconSize();
  // Painted in screen pixels, so that the icons stay sharp on scaled screens.
  const qreal dpr = m_pageList->devicePixelRatioF();
  QPixmap canvas(size * dpr);
  canvas.setDevicePixelRatio(dpr);
  canvas.fill(Qt::transparent);
  QPainter painter(&canvas);
  if (thumbnail.isNull()) {
    // Centered like the thumbnails, which keep their proportions.
    QSize placeholderSize = QSize(qRound(size.height() * m_placeholderAspect), size.height());
    if (placeholderSize.width() > size.width()) {
      placeholderSize = QSize(size.width(), qRound(size.width() / m_placeholderAspect));
    }
    placeholderSize = placeholderSize.expandedTo(QSize(2, 2));
    const QPoint topLeft((size.width() - placeholderSize.width()) / 2, (size.height() - placeholderSize.height()) / 2);
    const QRect rect = QRect(topLeft, placeholderSize).adjusted(0, 0, -1, -1);
    painter.fillRect(rect, QColor(220, 220, 220));
    painter.setPen(QColor(160, 160, 160));
    painter.drawRect(rect);
    if (missing) {
      // On the light grey of the placeholder, whatever the color scheme.
      painter.setFont(m_pageList->font());
      painter.setPen(QColor(80, 80, 80));
      painter.drawText(rect.adjusted(4, 4, -4, -4), Qt::AlignCenter | Qt::TextWordWrap, tr("Not output yet"));
    }
    if (missing || outdated) {
      // A page not output yet waits (hourglass); one changed since the output needs to be output again.
      const ThumbnailBase::Mark mark = missing ? ThumbnailBase::INCOMPLETE_MARK : ThumbnailBase::OUTDATED_MARK;
      ThumbnailBase::drawMark(painter, ThumbnailBase::markRect(rect), mark);
    }
  } else {
    QPixmap scaled = thumbnail.scaled(size * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    const QSize scaledSize = scaled.deviceIndependentSize().toSize();
    const QPoint topLeft((size.width() - scaledSize.width()) / 2, (size.height() - scaledSize.height()) / 2);
    painter.drawPixmap(topLeft, scaled);
    painter.setPen(QColor(160, 160, 160));
    const QRect rect = QRect(topLeft, scaledSize).adjusted(0, 0, -1, -1);
    painter.drawRect(rect);
    if (outdated) {
      // The output file shows an older state of the page.
      ThumbnailBase::drawMark(painter, ThumbnailBase::markRect(rect), ThumbnailBase::OUTDATED_MARK);
    }
  }
  painter.end();
  return QIcon(canvas);
}

QString ExportView::pdfMethodName(const QComboBox* box) const {
  return (box->currentData().toInt() == static_cast<int>(PdfCompression::NONE)) ? tr("uncompressed")
                                                                                : box->currentText();
}

const QComboBox* ExportView::pictureCompressionBox(const ExportPage& page) const {
  switch (page.kind()) {
    case ExportPage::BITONAL:
      return m_bitonalCompression;
    case ExportPage::PALETTE:
      return m_paletteCompression;
    case ExportPage::MRC:
      return page.hasPalettePicture() ? m_paletteCompression : m_colorCompression;
    default:
      return m_colorCompression;
  }
}

const QSpinBox* ExportView::pictureQualityBox(const ExportPage& page) const {
  return (pictureCompressionBox(page) == m_paletteCompression) ? m_paletteQuality : m_colorQuality;
}

ExportView::PageQuality ExportView::pageQuality(const ExportPage& page) const {
  PageQuality quality;
  if (page.kind() == ExportPage::MISSING) {
    return quality;
  }
  const QComboBox* box = pictureCompressionBox(page);
  quality.part = (box == m_colorCompression) ? 0 : (box == m_paletteCompression) ? 1 : 2;
  quality.fileProblem = !page.warning().isEmpty();
  quality.tiffPictureLossy = ExportPage::isLossyTiffCompression(page.pictureCompression());
  quality.tiffTextLossy
      = (page.kind() == ExportPage::MRC) && ExportPage::isLossyTiffCompression(page.textCompression());
  // The black and white methods of the PDF are all lossless.
  if (page.kind() != ExportPage::BITONAL) {
    const auto compression = static_cast<PdfCompression>(box->currentData().toInt());
    quality.pdfLossy = isLossyPdfCompression(compression, pictureQualityBox(page)->value());
  }
  quality.posterizedAsJpeg = (box == m_paletteCompression) && usesQuality(m_paletteCompression);
  quality.withoutSplitOutput = page.isWithoutSplitOutput();
  return quality;
}

QStringList ExportView::pageWarnings(const ExportPage& page, const PageQuality& quality) const {
  QStringList warnings;
  if (quality.fileProblem) {
    warnings.push_back(page.warning());
  }
  // Losses in the PDF alone are chosen in the PDF Compression panel and aren't warned about.
  if (quality.tiffPictureLossy) {
    const QString tiffMethod = ExportPage::tiffCompressionName(page.pictureCompression());
    const QString pdfMethod = pdfMethodName(pictureCompressionBox(page));
    const QString stored
        = (page.kind() == ExportPage::MRC)
              ? tr("In the output folder, the picture of this page is stored with lossy compression (%1).")
                    .arg(tiffMethod)
              : tr("In the output folder, this page is stored with lossy compression (%1).").arg(tiffMethod);
    const QString inPdf
        = quality.pdfLossy
              ? tr("The PDF compresses it with loss a second time (%1), so the losses add up.").arg(pdfMethod)
              : tr("The PDF stores it losslessly (%1), which keeps the losses and makes the file larger than "
                   "necessary.")
                    .arg(pdfMethod);
    warnings.push_back(stored + ' ' + inPdf);
  }
  if (quality.tiffTextLossy) {
    warnings.push_back(tr("In the output folder, the text of this page is stored with lossy compression (%1). The "
                          "PDF can't undo these losses.")
                           .arg(ExportPage::tiffCompressionName(page.textCompression())));
  }
  if (quality.posterizedAsJpeg) {
    warnings.push_back(tr("%1 converts the posterized picture of this page to full color, which blurs the color "
                          "areas and often makes the file larger. Deflate keeps the colors exactly.")
                           .arg(pdfMethodName(m_paletteCompression)));
  }
  if (quality.withoutSplitOutput && quality.pdfLossy) {
    warnings.push_back(tr("Mixed page without split output. It is stored as a single image, so with %1 the text is "
                          "less sharp and the PDF is larger. Enable \"Split output\" in the Output step and process "
                          "the page again.")
                           .arg(pdfMethodName(pictureCompressionBox(page))));
  }
  return warnings;
}  // ExportView::pageWarnings

QStringList ExportView::pageNotes(const PageQuality& quality) const {
  QStringList notes;
  // Without losses, only the size of the PDF is at stake.
  if (quality.withoutSplitOutput && !quality.pdfLossy) {
    notes.push_back(
        tr("Mixed page without split output. It is stored losslessly as a single image. With split output, the "
           "PDF is usually much smaller."));
  }
  return notes;
}

QString ExportView::pageToolTip(const int entryIndex) const {
  if ((entryIndex < 0) || (entryIndex >= static_cast<int>(m_entries.size()))) {
    return QString();
  }
  const ExportPage& page = m_entries[entryIndex].page;
  if (page.kind() == ExportPage::MISSING) {
    return tr("Not output yet. Process the page in the Output step to include it in the export.");
  }
  const PageQuality quality = pageQuality(page);
  const QStringList warnings = pageWarnings(page, quality);
  const QStringList notes = pageNotes(quality);
  // A method, marked if it loses quality.
  auto tiffMethod = [](const int compression) {
    const QString name = ExportPage::tiffCompressionName(compression);
    return ExportPage::isLossyTiffCompression(compression) ? tr("%1 (lossy)", "tooltip").arg(name) : name;
  };
  auto pdfMethod = [this](const QComboBox* box, const bool lossy) {
    const QString name = pdfMethodName(box);
    return lossy ? tr("%1 (lossy)", "tooltip").arg(name) : name;
  };

  QString mode;
  QString tiff;
  QString pdf;
  const QString picturePdf = pdfMethod(pictureCompressionBox(page), quality.pdfLossy);
  switch (page.kind()) {
    case ExportPage::BITONAL:
      mode = tr("Black and white");
      tiff = tiffMethod(page.pictureCompression());
      pdf = pdfMethod(m_bitonalCompression, false);
      break;
    case ExportPage::MRC:
      mode = tr("Mixed, split output");
      tiff = tr("%1 (picture), %2 (text)")
                 .arg(tiffMethod(page.pictureCompression()), tiffMethod(page.textCompression()));
      pdf = tr("%1 (picture), %2 (text)").arg(picturePdf, pdfMethod(m_bitonalCompression, false));
      break;
    default:
      if (page.isMixedMode()) {
        mode = tr("Mixed");
      } else if (page.kind() == ExportPage::PALETTE) {
        mode = tr("Color / grayscale, posterized");
      } else {
        mode = tr("Color / grayscale");
      }
      tiff = tiffMethod(page.pictureCompression());
      pdf = picturePdf;
      break;
  }
  QStringList lines;
  if (page.isOutdated()) {
    lines.push_back(
        tr("Changed since the output. The export would show the state of the last output. Process the page in the "
           "Output step to include the changes."));
    lines.push_back(QString());
  }
  lines.push_back(tr("Color mode: %1").arg(mode));
  lines.push_back(tr("Compression TIFF: %1").arg(tiff));
  lines.push_back(tr("Compression PDF: %1").arg(pdf));
  if (!warnings.isEmpty() || !notes.isEmpty()) {
    lines.push_back(QString());
  }
  for (const QString& warning : warnings) {
    lines.push_back(warningSign() + ' ' + warning);
  }
  for (const QString& note : notes) {
    lines.push_back(informationSign() + ' ' + note);
  }
  return lines.join('\n');
}  // ExportView::pageToolTip

void ExportView::updatePageSigns() {
  // Not a change by the user.
  const QSignalBlocker blocker(m_pageList);
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    const PageQuality quality = pageQuality(m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page);
    item->setData(SIGN_ROLE, quality.hasWarnings() ? WARNING_SIGN : quality.hasNotes() ? INFORMATION_SIGN : NO_SIGN);
  }
  m_pageList->viewport()->update();
}

void ExportView::updateQualityPanel() {
  if (!m_qualityLayout) {
    return;
  }
  // Color / grayscale, posterized, black and white: the pages for each TIFF compression, and in the PDF.
  std::map<int, int> tiffCounts[3] = {};
  int pdfCounts[3] = {};
  int pages = 0;
  int tiffLossyPages = 0;
  int twiceLossyPages = 0;
  int posterizedAsJpegPages = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (!item->flags().testFlag(Qt::ItemIsUserCheckable) || (item->checkState() != Qt::Checked)) {
      continue;
    }
    const ExportPage& page = m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page;
    const PageQuality quality = pageQuality(page);
    ++tiffCounts[quality.part][page.pictureCompression()];
    ++pdfCounts[quality.part];
    if (page.kind() == ExportPage::MRC) {
      ++tiffCounts[2][page.textCompression()];
      ++pdfCounts[2];
    }
    ++pages;
    if (quality.tiffPictureLossy || quality.tiffTextLossy) {
      ++tiffLossyPages;
    }
    if (quality.tiffPictureLossy && quality.pdfLossy) {
      ++twiceLossyPages;
    }
    if (quality.posterizedAsJpeg) {
      ++posterizedAsJpegPages;
    }
  }

  const QComboBox* pdfBoxes[3] = {m_colorCompression, m_paletteCompression, m_bitonalCompression};
  const QSpinBox* qualityBoxes[3] = {m_colorQuality, m_paletteQuality, nullptr};
  for (int part = 0; part < 3; ++part) {
    QStringList methods;
    for (const auto& [compression, count] : tiffCounts[part]) {
      QString method = ExportPage::tiffCompressionName(compression);
      if (ExportPage::isLossyTiffCompression(compression)) {
        method = tr("%1 (lossy)", "short").arg(method);
      }
      methods.push_back(QStringLiteral(u"%1 × %2").arg(count).arg(method));
    }
    m_tiffQuality[part]->setText(methods.join(QStringLiteral(", ")));

    QString pdfMethod = pdfMethodName(pdfBoxes[part]);
    const auto compression = static_cast<PdfCompression>(pdfBoxes[part]->currentData().toInt());
    if (qualityBoxes[part] && isLossyPdfCompression(compression, qualityBoxes[part]->value())) {
      pdfMethod = tr("%1 (lossy)", "short").arg(pdfMethod);
    }
    m_pdfQuality[part]->setText(QStringLiteral(u"%1 × %2").arg(pdfCounts[part]).arg(pdfMethod));

    m_qualityLayout->setRowVisible(m_tiffQuality[part], pdfCounts[part] > 0);
    m_qualityLayout->setRowVisible(m_pdfQuality[part], pdfCounts[part] > 0);
  }
  m_qualityLayout->setRowVisible(m_tiffNoPages, pages == 0);
  m_qualityLayout->setRowVisible(m_pdfNoPages, pages == 0);

  const QString sign = warningSign() + ' ';
  m_tiffWarning->setText(sign + tr("Stored with lossy compression: %1").arg(tiffLossyPages));
  m_tiffWarning->setToolTip(
      tr("The PDF can't undo these losses. A lossless method in the PDF keeps them and makes the file larger than "
         "necessary. To avoid the losses, choose a lossless method in the TIFF Compression panel of the Output "
         "step and process the pages again."));
  m_qualityLayout->setRowVisible(m_tiffWarning, tiffLossyPages > 0);

  QStringList pdfWarnings;
  QStringList pdfToolTips;
  if (twiceLossyPages > 0) {
    pdfWarnings.push_back(sign + tr("Compressed with loss twice: %1").arg(twiceLossyPages));
    pdfToolTips.push_back(
        tr("The output files of these pages are stored with lossy compression, and the PDF compresses them with "
           "loss a second time, so the losses add up. Deflate, or JPEG 2000 at quality 100, avoids the second "
           "loss."));
  }
  if (posterizedAsJpegPages > 0) {
    pdfWarnings.push_back(sign + tr("Posterized, converted to full color: %1").arg(posterizedAsJpegPages));
    pdfToolTips.push_back(
        tr("JPEG and JPEG 2000 convert posterized pictures to full color, which blurs the color areas and often "
           "makes the file larger. Deflate keeps the colors exactly."));
  }
  m_pdfWarning->setText(pdfWarnings.join('\n'));
  m_pdfWarning->setToolTip(pdfToolTips.join('\n'));
  m_qualityLayout->setRowVisible(m_pdfWarning, !pdfWarnings.isEmpty());
}  // ExportView::updateQualityPanel

void ExportView::compressionChanged() {
  saveSettings();
  updatePageSigns();
  updateQualityPanel();
}

int ExportView::checkedCount() const {
  int count = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      ++count;
    }
  }
  return count;
}

void ExportView::setRunning(const bool running) {
  m_pageList->setEnabled(!running);
  m_allButton->setEnabled(!running);
  m_noneButton->setEnabled(!running);
  m_folderEdit->setEnabled(!running);
  m_nameEdit->setEnabled(!running);
  m_browseButton->setEnabled(!running);
  m_optionsWidget->setEnabled(!running);
  m_createButton->setVisible(!running);
  m_cancelButton->setVisible(running);
  m_cancelButton->setEnabled(running);
  updateControls();
  emit runningChanged(running);
}
