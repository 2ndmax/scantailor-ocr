// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfExportView.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
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
#include "ImageId.h"
#include "OcrEngine.h"
#include "OcrLanguages.h"
#include "OcrPanel.h"
#include "PdfCompressionPanel.h"
#include "PdfExportJob.h"
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
  return ColorSchemeManager::instance().getColorParam("ThumbnailSequenceSelectedItemBorder",
                                                      QApplication::palette().color(QPalette::Highlight).darker(150));
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
class PdfExportView::PageTileDelegate : public QStyledItemDelegate {
 public:
  /** param view Builds the tooltips when they are shown. */
  PageTileDelegate(const PdfExportView* view, QObject* parent) : QStyledItemDelegate(parent), m_view(view) {}

  QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override {
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
      // A ring in the accent color, as the frame of the button that creates the PDF.
      painter->setPen(QPen(accentColor(), 2));
      painter->setBrush(Qt::NoBrush);
      const QRectF ring = QRectF(badge).adjusted(1, 1, -1, -1);
      const double ringRadius = rounded ? ring.height() / 2.0 : 0.0;
      painter->drawRoundedRect(ring, ringRadius, ringRadius);
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

  const PdfExportView* m_view;
};

class PdfExportView::ThumbnailHandler : public ThumbnailPixmapCache::CompletionHandler {
 public:
  ThumbnailHandler(PdfExportView* view, const int generation, const int entryIndex)
      : m_view(view), m_generation(generation), m_entryIndex(entryIndex) {}

  void operator()(const ThumbnailLoadResult& result) override {
    m_view->thumbnailLoaded(m_generation, m_entryIndex, result);
  }

 private:
  PdfExportView* m_view;
  int m_generation;
  int m_entryIndex;
};


PdfExportView::PdfExportView(std::shared_ptr<ThumbnailPixmapCache> thumbnailCache,
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
  m_allButton->setToolTip(tr("Include all output pages in the PDF."));
  m_noneButton->setToolTip(tr("Exclude all pages from the PDF."));
  m_forwardButton->setToolTip(
      tr("Move the selected pages one place towards the start of the PDF. Pages can also be moved with drag "
         "and drop."));
  m_backButton->setToolTip(
      tr("Move the selected pages one place towards the end of the PDF. Pages can also be moved with drag and "
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

  // Target file.
  auto* fileLayout = new QHBoxLayout;
  fileLayout->addWidget(new QLabel(tr("PDF file:")));
  m_fileEdit = new QLineEdit;
  fileLayout->addWidget(m_fileEdit, 1);
  m_browseButton = new QPushButton(tr("Browse ..."));
  fileLayout->addWidget(m_browseButton);
  mainLayout->addLayout(fileLayout);

  // The action in one row: what happens afterwards, the progress and the button that starts it.
  auto* buttons = new QHBoxLayout;
  // About what happens after creating, not about the content of the PDF, so next to the button.
  m_openAfterCreation = new QCheckBox(tr("Open the PDF after creating it"));
  m_openAfterCreation->setChecked(ApplicationSettings::getInstance().isPdfOpenAfterCreationEnabled());
  buttons->addWidget(m_openAfterCreation);
  buttons->addSpacing(12);
  m_progressBar = new QProgressBar;
  m_progressBar->setRange(0, 1);
  m_progressBar->setValue(0);
  m_progressBar->setFormat("%v / %m");
  buttons->addWidget(m_progressBar, 1);
  buttons->addSpacing(12);
  m_cancelButton = new QPushButton(tr("Cancel"));
  m_cancelButton->setVisible(false);
  m_createButton = new AccentFrameButton(tr("Create PDF"));
  m_createButton->setDefault(true);
  buttons->addWidget(m_cancelButton);
  buttons->addWidget(m_createButton);
  mainLayout->addLayout(buttons);
  m_statusLabel = new QLabel;
  m_statusLabel->setWordWrap(true);
  m_statusLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
  mainLayout->addWidget(m_statusLabel);

  m_optionsWidget.reset(createOptionsWidget());

  connect(m_pageList, &QListWidget::itemChanged, this, &PdfExportView::updatePositions);
  connect(m_pageList, &QListWidget::itemChanged, this, &PdfExportView::updateControls);
  connect(m_pageList, &QListWidget::itemSelectionChanged, this, &PdfExportView::updateControls);
  // Drag and drop as well as the buttons move items.
  connect(m_pageList->model(), &QAbstractItemModel::rowsMoved, this, &PdfExportView::updatePositions);
  connect(m_pageList->model(), &QAbstractItemModel::rowsInserted, this, &PdfExportView::updatePositions);
  connect(m_pageList->model(), &QAbstractItemModel::rowsRemoved, this, &PdfExportView::updatePositions);
  connect(m_allButton, &QPushButton::clicked, this, [this]() { setAllChecked(Qt::Checked); });
  connect(m_noneButton, &QPushButton::clicked, this, [this]() { setAllChecked(Qt::Unchecked); });
  connect(m_outputMissingButton, &QPushButton::clicked, this, &PdfExportView::outputMissingPagesRequested);
  connect(m_forwardButton, &QPushButton::clicked, this, &PdfExportView::moveForward);
  connect(m_backButton, &QPushButton::clicked, this, &PdfExportView::moveBack);
  connect(m_fileEdit, &QLineEdit::textEdited, this, [this]() { m_fileChosen = true; });
  connect(m_browseButton, &QPushButton::clicked, this, &PdfExportView::browse);
  connect(m_createButton, &QPushButton::clicked, this, &PdfExportView::startExport);
  connect(m_cancelButton, &QPushButton::clicked, this, &PdfExportView::cancelExport);
  connect(m_openAfterCreation, &QCheckBox::toggled, this, &PdfExportView::saveSettings);

  updateControls();
}

PdfExportView::~PdfExportView() {
  if (m_thread) {
    m_job->cancel();
    m_thread->wait();
    delete m_thread;
  }
}

QWidget* PdfExportView::createOptionsWidget() {
  auto* widget = new QWidget;
  // The default margins, as around the options of the other steps.
  auto* layout = new QVBoxLayout(widget);

  // Collapsible like the panels of the other steps; the object name keeps its collapsed state.
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
  connect(m_compressionPanel, &PdfCompressionPanel::compressionChanged, this, &PdfExportView::compressionChanged);

#ifdef ENABLE_OCR
  // Text recognition. Like the panels of the other steps, the check box that turns it on
  // is the first entry, not part of the title.
  auto* ocrGroup = new CollapsibleGroupBox(tr("Text Recognition (OCR)"));
  ocrGroup->setObjectName("pdfOcrPanel");
  auto* ocrGroupLayout = new QVBoxLayout(ocrGroup);
  m_ocrPanel = new OcrPanel;
  m_ocrPanel->setOptions(m_processingOptions->pdf);
  ocrGroupLayout->addWidget(m_ocrPanel);
  layout->addWidget(ocrGroup);
  connect(m_ocrPanel, &OcrPanel::optionsChanged, this, &PdfExportView::saveSettings);
#endif

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

  layout->addStretch(1);
  return widget;
}  // PdfExportView::createOptionsWidget

QWidget* PdfExportView::optionsWidget() const {
  return m_optionsWidget.get();
}

void PdfExportView::setPages(std::vector<Entry> entries) {
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
      const PdfExportPage& page = m_entries[i].page;
      auto* item = new QListWidgetItem(m_pageList);
      item->setData(ENTRY_INDEX_ROLE, i);
      item->setData(KEY_ROLE, orderItem.key);
      item->setText(m_entries[i].label);
      // Kept for pages that aren't output yet, for when they are.
      item->setCheckState(orderItem.checked ? Qt::Checked : Qt::Unchecked);
      if (page.kind() == PdfExportPage::MISSING) {
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
    if (m_entries[i].page.kind() != PdfExportPage::MISSING) {
      requestThumbnail(m_generation, i);
    }
  }
  updateControls();
}  // PdfExportView::setPages

void PdfExportView::setDefaultFile(const QString& file) {
  if (!m_fileChosen) {
    m_fileEdit->setText(QDir::toNativeSeparators(file));
  }
}

bool PdfExportView::isRunning() const {
  return m_thread != nullptr;
}

std::set<PageId> PdfExportView::pagesToOutput() const {
  std::vector<PdfExportPage> pages;
  for (const Entry& entry : m_entries) {
    pages.push_back(entry.page);
  }
  return PdfExportPage::pagesToOutput(pages);
}

void PdfExportView::cancelAndWait() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  m_thread->wait();
  exportFinished();
}

void PdfExportView::setAllChecked(const Qt::CheckState state) {
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

void PdfExportView::updatePositions() {
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

void PdfExportView::moveForward() {
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

void PdfExportView::moveBack() {
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

void PdfExportView::browse() {
  QString path = QFileDialog::getSaveFileName(this, tr("Save PDF As"), m_fileEdit->text(), tr("PDF files (*.pdf)"));
  if (path.isEmpty()) {
    return;
  }
  if (path.endsWith(".pdf", Qt::CaseInsensitive)) {
    // The file dialog has already asked about replacing an existing file.
    m_overwriteConfirmed = QDir::cleanPath(path);
  } else {
    path += ".pdf";
  }
  m_fileEdit->setText(QDir::toNativeSeparators(path));
  m_fileChosen = true;
}

void PdfExportView::startExport() {
  if (m_thread) {
    return;
  }
  const QString title = tr("Create PDF");

  QString path = QDir::fromNativeSeparators(m_fileEdit->text().trimmed());
  if (path.isEmpty()) {
    QMessageBox::warning(this, title, tr("Please enter the file name of the PDF."));
    return;
  }
  if (!path.endsWith(".pdf", Qt::CaseInsensitive)) {
    path += ".pdf";
  }
  const QFileInfo fileInfo(path);
  if (fileInfo.isRelative()) {
    QMessageBox::warning(this, title, tr("Please enter the complete path of the PDF, including the folder."));
    return;
  }
  path = QDir::cleanPath(fileInfo.absoluteFilePath());
  m_fileEdit->setText(QDir::toNativeSeparators(path));
  if (!fileInfo.absoluteDir().exists()) {
    QMessageBox::warning(this, title,
                         tr("The folder %1 doesn't exist.").arg(QDir::toNativeSeparators(fileInfo.absolutePath())));
    return;
  }
  if (fileInfo.isDir()) {
    QMessageBox::warning(this, title, tr("%1 is a folder.").arg(QDir::toNativeSeparators(path)));
    return;
  }
  // Asked first: whoever goes back to output the pages creates the PDF later.
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
                    tr("%n selected page(s) changed since the output. The PDF would show the state of the last "
                       "output.\n\nDo you want to create the PDF anyway? To include the changes, choose \"Back\" "
                       "and then \"Output missing and changed pages\".",
                       nullptr, outdated));
    QPushButton* createButton = box.addButton(tr("Create PDF anyway"), QMessageBox::AcceptRole);
    QPushButton* backButton = box.addButton(tr("Back"), QMessageBox::RejectRole);
    box.setDefaultButton(backButton);
    box.setEscapeButton(backButton);
    box.exec();
    if (box.clickedButton() != createButton) {
      return;
    }
  }

  // The confirmation of the file dialog only counts for the PDF created right after it.
  // Each further time an existing file is asked about again, even if this view created it:
  // it may have been changed outside the program since.
  const bool confirmedInDialog = (path == m_overwriteConfirmed);
  m_overwriteConfirmed.clear();
  if (fileInfo.exists() && !confirmedInDialog) {
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, title, tr("The file %1 already exists.\nDo you want to replace it?").arg(QDir::toNativeSeparators(path)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return;
    }
  }

  std::vector<PdfExportPage> pages;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      pages.push_back(m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page);
    }
  }
  if (pages.empty()) {
    QMessageBox::information(this, title, tr("Please tick the pages to include in the PDF."));
    return;
  }

  PdfExportOptions options = PdfExportOptions::fromProject(*m_processingOptions);
  options.threadCount = PdfExportJob::defaultThreadCount();

#ifdef ENABLE_OCR
  if (m_ocrPanel->isOcrEnabled()) {
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
    if (languages.isEmpty()) {
      QMessageBox::warning(this, title,
                           tr("Please tick at least one language for the text recognition, or turn it off."));
      return;
    }
    QString dataDir = OcrLanguages::commonDir(languages);
    if (dataDir.isEmpty()) {
      // Tesseract needs all languages in one folder, so the missing ones are copied
      // into the user's folder. That's only needed once.
      m_statusLabel->setText(tr("Copying language files ..."));
      QApplication::setOverrideCursor(Qt::WaitCursor);
      QString error;
      dataDir = OcrLanguages::gatherInUserDir(languages, &error);
      QApplication::restoreOverrideCursor();
      m_statusLabel->clear();
      if (dataDir.isEmpty()) {
        QMessageBox::warning(this, title,
                             tr("The files of the selected languages are in different folders, and they could "
                                "not be copied into one folder, which text recognition needs.")
                                 + QStringLiteral("\n\n") + error);
        return;
      }
    }
    options.ocr = true;
    options.ocrDataDir = dataDir;
    options.ocrLanguages = languages.join('+');
    options.ocrPageLayout = m_ocrPanel->pageLayout();
  }
#endif

  const int pageCount = static_cast<int>(pages.size());
  m_job = std::make_unique<PdfExportJob>(std::move(pages), path, options);
  connect(m_job.get(), &PdfExportJob::progress, this, &PdfExportView::exportProgress, Qt::QueuedConnection);
  PdfExportJob* job = m_job.get();
  m_thread = QThread::create([job]() { job->run(); });
  connect(m_thread, &QThread::finished, this, &PdfExportView::exportFinished);

  m_runningFile = path;
  m_progressBar->setRange(0, pageCount);
  m_progressBar->setValue(0);
  m_statusLabel->setText(options.ocr ? tr("Recognizing the text and creating the PDF ...")
                                     : tr("Creating the PDF ..."));
  setRunning(true);
  m_thread->start();
}  // PdfExportView::startExport

void PdfExportView::cancelExport() {
  if (!m_thread) {
    return;
  }
  m_job->cancel();
  m_statusLabel->setText(tr("Canceling ..."));
  m_cancelButton->setEnabled(false);
}

void PdfExportView::exportProgress(const int pagesDone, const int pagesTotal) {
  if (!m_thread) {
    return;
  }
  m_progressBar->setRange(0, pagesTotal);
  m_progressBar->setValue(pagesDone);
}

void PdfExportView::exportFinished() {
  if (!m_thread) {
    return;
  }
  m_thread->wait();
  delete m_thread;
  m_thread = nullptr;
  const std::unique_ptr<PdfExportJob> job = std::move(m_job);
  setRunning(false);

  const QString nativePath = QDir::toNativeSeparators(m_runningFile);
  if (job->succeeded()) {
    const QString size = QLocale().formattedDataSize(QFileInfo(m_runningFile).size());
    m_statusLabel->setText(tr("The PDF was created: %1 (%2)").arg(nativePath, size));
    if (m_openAfterCreation->isChecked()) {
      QDesktopServices::openUrl(QUrl::fromLocalFile(m_runningFile));
    }
  } else if (job->wasCancelled()) {
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("Canceled. No PDF was written."));
  } else {
    m_progressBar->setValue(0);
    m_statusLabel->setText(tr("The PDF could not be created."));
    QStringList errors = job->errors();
    const int maxShown = 20;
    if (errors.size() > maxShown) {
      const int hidden = static_cast<int>(errors.size()) - maxShown;
      errors = errors.mid(0, maxShown);
      errors.push_back(tr("... and %1 more.").arg(hidden));
    }
    QMessageBox::warning(this, tr("Create PDF"),
                         tr("The PDF could not be created.") + QStringLiteral("\n\n") + errors.join('\n'));
  }
}  // PdfExportView::exportFinished

void PdfExportView::updateControls() {
  const bool running = (m_thread != nullptr);
  int available = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    if (m_pageList->item(row)->flags().testFlag(Qt::ItemIsUserCheckable)) {
      ++available;
    }
  }
  const int checked = checkedCount();
  m_selectionLabel->setText(tr("%1 of %2 output pages selected").arg(checked).arg(available));
  const int missing = m_pageList->count() - available;
  if (missing > 0) {
    m_selectionLabel->setText(m_selectionLabel->text() + QStringLiteral("  ")
                              + tr("(%n page(s) not output yet)", nullptr, missing));
  }
  const int outdated = static_cast<int>(
      std::count_if(m_entries.begin(), m_entries.end(), [](const Entry& entry) { return entry.page.isOutdated(); }));
  if (outdated > 0) {
    m_selectionLabel->setText(m_selectionLabel->text() + QStringLiteral("  ")
                              + tr("(%n page(s) changed since the output)", nullptr, outdated));
  }

  m_outputMissingButton->setVisible((missing > 0) || (outdated > 0));
  m_outputMissingButton->setEnabled(!running);

  const bool hasSelection = !m_pageList->selectedItems().isEmpty();
  m_forwardButton->setEnabled(!running && hasSelection);
  m_backButton->setEnabled(!running && hasSelection);
  m_createButton->setEnabled(!running && (checked > 0));
  updateQualityPanel();
}

void PdfExportView::saveSettings() {
  // Only opening the PDF is a program setting; the rest belongs to the project.
  ApplicationSettings::getInstance().setPdfOpenAfterCreationEnabled(m_openAfterCreation->isChecked());
  const ProcessingOptions::Pdf before = m_processingOptions->pdf;
  m_compressionPanel->storeOptions(m_processingOptions->pdf);
#ifdef ENABLE_OCR
  m_ocrPanel->storeOptions(m_processingOptions->pdf);
#endif
  if (m_processingOptions->pdf != before) {
    // The project changed, so it gets saved automatically like other changes.
    emit projectOptionsChanged();
  }
}

void PdfExportView::requestThumbnail(const int generation, const int entryIndex) {
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

void PdfExportView::thumbnailLoaded(const int generation, const int entryIndex, const ThumbnailLoadResult& result) {
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

QListWidgetItem* PdfExportView::itemForEntry(const int entryIndex) const {
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    if (item->data(ENTRY_INDEX_ROLE).toInt() == entryIndex) {
      return item;
    }
  }
  return nullptr;
}

void PdfExportView::showThumbnail(const int entryIndex, const QPixmap& thumbnail) {
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
          const PdfExportPage& page = m_entries[i].page;
          item->setIcon(makeIcon(QPixmap(), page.kind() == PdfExportPage::MISSING, page.isOutdated()));
        }
      }
    }
  }
}

QIcon PdfExportView::makeIcon(const QPixmap& thumbnail, const bool missing, const bool outdated) const {
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
      ThumbnailBase::drawMark(painter, ThumbnailBase::markRect(rect), ThumbnailBase::INCOMPLETE_MARK);
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
      // The output file shows an older state of the page, as with the hourglass of the Output step.
      ThumbnailBase::drawMark(painter, ThumbnailBase::markRect(rect), ThumbnailBase::INCOMPLETE_MARK);
    }
  }
  painter.end();
  return QIcon(canvas);
}

QString PdfExportView::pdfMethodName(const QComboBox* box) const {
  return (box->currentData().toInt() == static_cast<int>(PdfCompression::NONE)) ? tr("uncompressed")
                                                                                : box->currentText();
}

const QComboBox* PdfExportView::pictureCompressionBox(const PdfExportPage& page) const {
  switch (page.kind()) {
    case PdfExportPage::BITONAL:
      return m_bitonalCompression;
    case PdfExportPage::PALETTE:
      return m_paletteCompression;
    case PdfExportPage::MRC:
      return page.hasPalettePicture() ? m_paletteCompression : m_colorCompression;
    default:
      return m_colorCompression;
  }
}

const QSpinBox* PdfExportView::pictureQualityBox(const PdfExportPage& page) const {
  return (pictureCompressionBox(page) == m_paletteCompression) ? m_paletteQuality : m_colorQuality;
}

PdfExportView::PageQuality PdfExportView::pageQuality(const PdfExportPage& page) const {
  PageQuality quality;
  if (page.kind() == PdfExportPage::MISSING) {
    return quality;
  }
  const QComboBox* box = pictureCompressionBox(page);
  quality.part = (box == m_colorCompression) ? 0 : (box == m_paletteCompression) ? 1 : 2;
  quality.fileProblem = !page.warning().isEmpty();
  quality.tiffPictureLossy = PdfExportPage::isLossyTiffCompression(page.pictureCompression());
  quality.tiffTextLossy
      = (page.kind() == PdfExportPage::MRC) && PdfExportPage::isLossyTiffCompression(page.textCompression());
  // The black and white methods of the PDF are all lossless.
  if (page.kind() != PdfExportPage::BITONAL) {
    const auto compression = static_cast<PdfCompression>(box->currentData().toInt());
    quality.pdfLossy = isLossyPdfCompression(compression, pictureQualityBox(page)->value());
  }
  quality.posterizedAsJpeg = (box == m_paletteCompression) && usesQuality(m_paletteCompression);
  quality.withoutSplitOutput = page.isWithoutSplitOutput();
  return quality;
}

QStringList PdfExportView::pageWarnings(const PdfExportPage& page, const PageQuality& quality) const {
  QStringList warnings;
  if (quality.fileProblem) {
    warnings.push_back(page.warning());
  }
  // Losses in the PDF alone are chosen in the PDF Compression panel and aren't warned about.
  if (quality.tiffPictureLossy) {
    const QString tiffMethod = PdfExportPage::tiffCompressionName(page.pictureCompression());
    const QString pdfMethod = pdfMethodName(pictureCompressionBox(page));
    const QString stored
        = (page.kind() == PdfExportPage::MRC)
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
                           .arg(PdfExportPage::tiffCompressionName(page.textCompression())));
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
}  // PdfExportView::pageWarnings

QStringList PdfExportView::pageNotes(const PageQuality& quality) const {
  QStringList notes;
  // Without losses, only the size of the PDF is at stake.
  if (quality.withoutSplitOutput && !quality.pdfLossy) {
    notes.push_back(
        tr("Mixed page without split output. It is stored losslessly as a single image. With split output, the "
           "PDF is usually much smaller."));
  }
  return notes;
}

QString PdfExportView::pageToolTip(const int entryIndex) const {
  if ((entryIndex < 0) || (entryIndex >= static_cast<int>(m_entries.size()))) {
    return QString();
  }
  const PdfExportPage& page = m_entries[entryIndex].page;
  if (page.kind() == PdfExportPage::MISSING) {
    return tr("Not output yet. Process the page in the Output step to include it in the PDF.");
  }
  const PageQuality quality = pageQuality(page);
  const QStringList warnings = pageWarnings(page, quality);
  const QStringList notes = pageNotes(quality);
  // A method, marked if it loses quality.
  auto tiffMethod = [](const int compression) {
    const QString name = PdfExportPage::tiffCompressionName(compression);
    return PdfExportPage::isLossyTiffCompression(compression) ? tr("%1 (lossy)", "tooltip").arg(name) : name;
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
    case PdfExportPage::BITONAL:
      mode = tr("Black and white");
      tiff = tiffMethod(page.pictureCompression());
      pdf = pdfMethod(m_bitonalCompression, false);
      break;
    case PdfExportPage::MRC:
      mode = tr("Mixed, split output");
      tiff = tr("%1 (picture), %2 (text)")
                 .arg(tiffMethod(page.pictureCompression()), tiffMethod(page.textCompression()));
      pdf = tr("%1 (picture), %2 (text)").arg(picturePdf, pdfMethod(m_bitonalCompression, false));
      break;
    default:
      if (page.isMixedMode()) {
        mode = tr("Mixed");
      } else if (page.kind() == PdfExportPage::PALETTE) {
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
        tr("Changed since the output. The PDF would show the state of the last output. Process the page in the "
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
}  // PdfExportView::pageToolTip

void PdfExportView::updatePageSigns() {
  // Not a change by the user.
  const QSignalBlocker blocker(m_pageList);
  for (int row = 0; row < m_pageList->count(); ++row) {
    QListWidgetItem* item = m_pageList->item(row);
    const PageQuality quality = pageQuality(m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page);
    item->setData(SIGN_ROLE, quality.hasWarnings() ? WARNING_SIGN : quality.hasNotes() ? INFORMATION_SIGN : NO_SIGN);
  }
  m_pageList->viewport()->update();
}

void PdfExportView::updateQualityPanel() {
  if (!m_qualityLayout) {
    return;
  }
  // Color / grayscale, posterized, black and white: the pages for each TIFF compression, and in the PDF.
  std::map<int, int> tiffCounts[3];
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
    const PdfExportPage& page = m_entries[item->data(ENTRY_INDEX_ROLE).toInt()].page;
    const PageQuality quality = pageQuality(page);
    ++tiffCounts[quality.part][page.pictureCompression()];
    ++pdfCounts[quality.part];
    if (page.kind() == PdfExportPage::MRC) {
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
      QString method = PdfExportPage::tiffCompressionName(compression);
      if (PdfExportPage::isLossyTiffCompression(compression)) {
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
}  // PdfExportView::updateQualityPanel

void PdfExportView::compressionChanged() {
  saveSettings();
  updatePageSigns();
  updateQualityPanel();
}

int PdfExportView::checkedCount() const {
  int count = 0;
  for (int row = 0; row < m_pageList->count(); ++row) {
    const QListWidgetItem* item = m_pageList->item(row);
    if (item->flags().testFlag(Qt::ItemIsUserCheckable) && (item->checkState() == Qt::Checked)) {
      ++count;
    }
  }
  return count;
}

void PdfExportView::setRunning(const bool running) {
  m_pageList->setEnabled(!running);
  m_allButton->setEnabled(!running);
  m_noneButton->setEnabled(!running);
  m_fileEdit->setEnabled(!running);
  m_browseButton->setEnabled(!running);
  m_optionsWidget->setEnabled(!running);
  m_cancelButton->setVisible(running);
  m_cancelButton->setEnabled(running);
  updateControls();
  emit runningChanged(running);
}
