// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FilterUi.h"

#include <utility>

#include <DewarpingPointMapper.h>
#include <PolygonUtils.h>
#include "DespeckleView.h"
#include "DewarpingView.h"
#include "ErrorWidget.h"
#include "FillZoneEditor.h"
#include "Filter.h"
#include "FilterUiInterface.h"
#include "ImageView.h"
#include "OptionsWidget.h"
#include "PageInfo.h"
#include "PictureZoneEditor.h"
#include "Settings.h"
#include "TabbedImageView.h"
#include "TaskResult.h"
#include "Thumbnail.h"
#include "ThumbnailData.h"
#include "Utils.h"

using namespace imageproc;
using namespace dewarping;

namespace output {
FilterUi::FilterUi(std::shared_ptr<Filter> filter, const PageSelectionAccessor& pageSelectionAccessor)
    : m_filter(std::move(filter)), m_pageSelectionAccessor(pageSelectionAccessor) {}

FilterUi::~FilterUi() = default;

void FilterUi::preUpdateUI(FilterUiInterface* ui, const PageInfo& pageInfo) {
  optionsWidget()->preUpdateUI(pageInfo.id());
  ui->setOptionsWidget(optionsWidget(), ui->KEEP_OWNERSHIP);
}

void FilterUi::showResult(FilterResult& result, FilterUiInterface* ui) {
  // This function is executed from the GUI thread.
  auto* const pageResult = dynamic_cast<TaskResult*>(&result);
  if (!pageResult) {
    return;
  }

  OptionsWidget* const optWidget = optionsWidget();
  optWidget->postUpdateUI();
  ui->setOptionsWidget(optWidget, ui->KEEP_OWNERSHIP);

  auto tabImageRectMap = std::make_unique<std::unordered_map<ImageViewTab, QRectF, std::hash<int>>>();

  auto imageView = std::make_unique<ImageView>(pageResult->outputImage(), pageResult->downscaledOutputImage());
  const QPixmap downscaledOutputPixmap(imageView->downscaledPixmap());
  tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_OUTPUT, pageResult->xform().resultingRect()));
  auto dewarpingView = std::make_unique<DewarpingView>(
      pageResult->origImage(), pageResult->downscaledOrigImage(), pageResult->xform().transform(),
      PolygonUtils::convexHull(std::vector<QPointF>(pageResult->xform().resultingPreCropArea().begin(),
                                                    pageResult->xform().resultingPreCropArea().end())),
      pageResult->virtContentRect(), pageResult->pageId(), pageResult->params().dewarpingOptions(),
      pageResult->params().distortionModel(), optWidget->depthPerception());
  const QPixmap downscaledOrigPixmap(dewarpingView->downscaledPixmap());
  QObject::connect(optWidget, &OptionsWidget::depthPerceptionChanged, dewarpingView.get(),
                   &DewarpingView::depthPerceptionChanged);
  QObject::connect(dewarpingView.get(), &DewarpingView::distortionModelChanged, optWidget,
                   &OptionsWidget::distortionModelChanged);
  tabImageRectMap->insert(
      std::pair<ImageViewTab, QRectF>(TAB_DEWARPING, pageResult->xform().resultingPreCropArea().boundingRect()));

  std::unique_ptr<QWidget> pictureZoneEditor;
  if (pageResult->pictureMask().isNull()) {
    pictureZoneEditor = std::make_unique<ErrorWidget>(tr("Picture zones are only available in Mixed mode."));
  } else {
    pictureZoneEditor = std::make_unique<output::PictureZoneEditor>(
        pageResult->origImage(), downscaledOrigPixmap, pageResult->pictureMask(), pageResult->xform().transform(),
        pageResult->xform().resultingPreCropArea(), pageResult->pageId(), pageResult->settings());
    QObject::connect(static_cast<PictureZoneEditor*>(pictureZoneEditor.get()), &PictureZoneEditor::invalidateThumbnail,
                     optWidget, qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail));
    tabImageRectMap->insert(
        std::pair<ImageViewTab, QRectF>(TAB_PICTURE_ZONES, pageResult->xform().resultingPreCropArea().boundingRect()));
  }

  // We make sure we never need to update the original <-> output
  // mapping at run time, that is without reloading.
  // In OptionsWidget::dewarpingChanged() we make sure to reload
  // if we are on the "Fill Zones" tab, and if not, it will be reloaded
  // anyway when another tab is selected.
  std::function<QPointF(const QPointF&)> origToOutput;
  std::function<QPointF(const QPointF&)> outputToOrig;
  if ((pageResult->params().dewarpingOptions().dewarpingMode() != OFF)
      && pageResult->params().distortionModel().isValid()) {
    const QTransform rotateXform = Utils::rotate(pageResult->params().dewarpingOptions().getPostDeskewAngle(),
                                                 pageResult->xform().resultingRect().toRect());
    // The same model domain as OutputGenerator::dewarp() uses.
    auto mapper = std::make_shared<DewarpingPointMapper>(
        pageResult->params().distortionModel(), pageResult->params().depthPerception().value(),
        pageResult->xform().transform(), pageResult->xform().resultingRect().toRect(), rotateXform);
    origToOutput = std::bind(&DewarpingPointMapper::mapToDewarpedSpace, mapper, std::placeholders::_1);
    outputToOrig = std::bind(&DewarpingPointMapper::mapToWarpedSpace, mapper, std::placeholders::_1);
  } else {
    using MapPointFunc = QPointF (QTransform::*)(const QPointF&) const;
    origToOutput = std::bind((MapPointFunc) &QTransform::map, pageResult->xform().transform(), std::placeholders::_1);
    outputToOrig
        = std::bind((MapPointFunc) &QTransform::map, pageResult->xform().transformBack(), std::placeholders::_1);
  }

  auto fillZoneEditor
      = std::make_unique<FillZoneEditor>(pageResult->outputImage(), downscaledOutputPixmap, origToOutput, outputToOrig,
                                         pageResult->pageId(), pageResult->settings());
  QObject::connect(fillZoneEditor.get(), &FillZoneEditor::invalidateThumbnail, optWidget,
                   qOverload<const PageId&>(&OptionsWidget::invalidateThumbnail));
  tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_FILL_ZONES, pageResult->xform().resultingRect()));

  std::unique_ptr<QWidget> despeckleView;
  if (pageResult->params().colorParams().colorMode() == COLOR_GRAYSCALE) {
    despeckleView = std::make_unique<ErrorWidget>(tr("Despeckling can't be done in Color / Grayscale mode."));
  } else {
    despeckleView = std::make_unique<output::DespeckleView>(pageResult->despeckleState(),
                                                            pageResult->despeckleVisualization(), pageResult->debug());
    QObject::connect(optWidget, &OptionsWidget::despeckleLevelChanged, static_cast<DespeckleView*>(despeckleView.get()),
                     &DespeckleView::despeckleLevelChanged);
    tabImageRectMap->insert(std::pair<ImageViewTab, QRectF>(TAB_DESPECKLING, pageResult->xform().resultingRect()));
  }

  auto tabWidget = std::make_unique<TabbedImageView>();
  tabWidget->setDocumentMode(true);
  tabWidget->setTabPosition(QTabWidget::East);
  tabWidget->addTab(imageView.release(), tr("Output"), TAB_OUTPUT,
                    tr("Shows the processed page as it will be saved. (Ctrl+1)"));
  tabWidget->addTab(pictureZoneEditor.release(), tr("Picture Zones"), TAB_PICTURE_ZONES,
                    tr("Marks the areas of the page that are treated as pictures. Only in Mixed mode. (Ctrl+2)"));
  tabWidget->addTab(fillZoneEditor.release(), tr("Fill Zones"), TAB_FILL_ZONES,
                    tr("Fills areas of the output with a color you pick. (Ctrl+3)"));
  tabWidget->addTab(dewarpingView.release(), tr("Dewarping"), TAB_DEWARPING,
                    tr("Shows the curves used to dewarp the page. In Manual mode, you place them yourself. (Ctrl+4)"));
  tabWidget->addTab(despeckleView.release(), tr("Despeckling"), TAB_DESPECKLING,
                    tr("Previews the removal of specks. (Ctrl+5)"));
  tabWidget->setCurrentTab(optWidget->lastTab());
  tabWidget->setImageRectMap(std::move(tabImageRectMap));

  QObject::connect(tabWidget.get(), &TabbedImageView::tabChanged, optWidget, &OptionsWidget::tabChanged);

  ui->setImageWidget(tabWidget.release(), ui->TRANSFER_OWNERSHIP, pageResult->dbg());
}

OptionsWidget* FilterUi::optionsWidget() {
  if (m_optionsWidget.get() == nullptr) {
    m_optionsWidget.reset(new OptionsWidget(m_filter->settings(), m_filter->processingOptions(),
                                            m_pageSelectionAccessor, m_filter->lastTab()));
  }
  return m_optionsWidget.get();
}

std::unique_ptr<QGraphicsItem> FilterUi::createThumbnail(const ThumbnailDataBase& data,
                                                         std::shared_ptr<ThumbnailPixmapCache> cache,
                                                         const QSizeF& maxSize) const {
  const auto* stepData = dynamic_cast<const ThumbnailData*>(&data);
  if (!stepData) {
    return nullptr;
  }
  return std::make_unique<Thumbnail>(std::move(cache), maxSize, stepData->imageId(), stepData->xform());
}
}  // namespace output
