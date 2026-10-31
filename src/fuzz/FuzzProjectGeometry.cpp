// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.
// F7: the page geometry of a project file. The input is the project file. Seeds: corpus/geometry,
// dictionary: geometry.dict.
//
// The settings of the steps up to the Output step are read as the program reads them
// (Settings::load()), then each page's geometry is built from them as the tasks build it, without
// an image: the rotation and trim of Fix Orientation, the split line of Split Pages, the angles of
// Deskew, the boxes of Select Content, the margins, alignment and output size of Margins, and the
// output resolution. The image of a page has the size and resolution its project entry gives;
// pages the program would not process (size above the limit, a resolution it asks to fix) are
// skipped.
//
// Pages without settings of their own get the start values first, as StageSequence gives them to
// new pages: those of the project (<default-params>), else the built-in ones. A project without
// Margins settings stands for a new project started with these start values (a profile), so it
// also takes their output size.
//
// Where a task would measure something on the image (no split line, no angle, no content box
// stored), the stored values are taken as if they still matched, and missing ones are replaced
// by the whole image and an angle of 0.
//
// Besides crashes, these count as findings, as the program relies on them: the trimmed image lies
// inside the image (ImageTrim::toInnerRect() promises it), Split Pages leaves the page a part of
// the image (the estimators of Deskew throw otherwise), every step leaves a geometry of finite
// numbers, and the output starts at 0, 0 (OutputGenerator asserts it).
#include <DefaultParams.h>
#include <ImageSizeLimit.h>
#include <ImageTransformation.h>
#include <PageId.h>

#include <QDir>
#include <QDomDocument>
#include <QPolygonF>
#include <QTransform>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <new>

#include "FuzzSetup.h"
#include "ImageSettings.h"
#include "PageSequence.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "filters/deskew/Params.h"
#include "filters/deskew/Settings.h"
#include "filters/deskew/Utils.h"
#include "filters/fix_orientation/Settings.h"
#include "filters/fix_orientation/Utils.h"
#include "filters/output/OutputGenerator.h"
#include "filters/output/Params.h"
#include "filters/output/Settings.h"
#include "filters/output/Utils.h"
#include "filters/page_layout/Params.h"
#include "filters/page_layout/Settings.h"
#include "filters/page_layout/Utils.h"
#include "filters/page_split/Dependencies.h"
#include "filters/page_split/PageLayoutAdapter.h"
#include "filters/page_split/Settings.h"
#include "filters/page_split/Utils.h"
#include "filters/select_content/Params.h"
#include "filters/select_content/Settings.h"
#include "filters/select_content/Utils.h"

namespace {
// More pages only repeat the work; a few are enough for the sizes that pages share in Margins.
const size_t MAX_PAGES = 16;

// A broken rule: reported like a crash, so that libFuzzer keeps the input.
void check(const bool ok, const char* rule) {
  if (!ok) {
    std::fprintf(stderr, "Rule broken: %s\n", rule);
    std::abort();
  }
}

bool finite(const QPointF& p) {
  return std::isfinite(p.x()) && std::isfinite(p.y());
}

bool finite(const QPolygonF& poly) {
  for (const QPointF& p : poly) {
    if (!finite(p)) {
      return false;
    }
  }
  return true;
}

bool finite(const QRectF& rect) {
  return finite(rect.topLeft()) && finite(rect.bottomRight());
}

bool finite(const QTransform& t) {
  return std::isfinite(t.m11()) && std::isfinite(t.m12()) && std::isfinite(t.m13()) && std::isfinite(t.m21())
         && std::isfinite(t.m22()) && std::isfinite(t.m23()) && std::isfinite(t.m31()) && std::isfinite(t.m32())
         && std::isfinite(t.m33());
}

void checkGeometry(const ImageTransformation& xform, const char* rule) {
  check(finite(xform.transform()) && finite(xform.transformBack()) && finite(xform.resultingRect())
            && finite(xform.resultingPreCropArea()) && finite(xform.resultingPostCropArea()),
        rule);
}

struct Steps {
  fix_orientation::Settings fixOrientation;
  ImageSettings fixOrientationImageSettings;
  page_split::Settings pageSplit;
  deskew::Settings deskew;
  ImageSettings deskewImageSettings;
  select_content::Settings selectContent;
  page_layout::Settings pageLayout;
  // Only the output resolution is used.
  output::Settings output;
  DefaultParams defaults;
};

// As the loadDefaultSettings() of the filters: only pages without settings of their own get them.
// Split Pages only takes the layout type from them, which matters only once a split line is found
// on the image.
void loadDefaultSettings(Steps& steps, const PageInfo& page) {
  if (steps.fixOrientation.isRotationNull(page.imageId())) {
    steps.fixOrientation.applyRotation(page.imageId(),
                                       fix_orientation::Utils::getDefaultOrthogonalRotation(steps.defaults));
  }
  if (steps.deskew.isParamsNull(page.id())) {
    steps.deskew.setPageParams(page.id(), deskew::Utils::buildDefaultParams(steps.defaults));
  }
  if (steps.selectContent.isParamsNull(page.id())) {
    steps.selectContent.setPageParams(page.id(),
                                      select_content::Utils::buildDefaultParams(page.metadata().dpi(), steps.defaults));
  }
  if (steps.pageLayout.isParamsNull(page.id())) {
    steps.pageLayout.setPageParams(page.id(),
                                   page_layout::Utils::buildDefaultParams(page.metadata().dpi(), steps.defaults));
  }
  if (steps.output.isParamsNull(page.id())) {
    steps.output.setParams(page.id(), output::Utils::buildDefaultParams(steps.defaults));
  }
}

// As select_content::Task::process() without detection: a page box from the start values only has
// a size and goes into the middle of the page; the content box is the page box.
void selectContentBoxes(const select_content::Params& params,
                        const ImageTransformation& xform,
                        QRectF& pageRect,
                        QRectF& contentRect) {
  pageRect = params.pageRect();
  contentRect = params.contentRect();
  if (!params.dependencies().rotatedPageOutline().empty() || contentRect.isValid()) {
    return;
  }
  if (params.pageDetectionMode() == MODE_MANUAL) {
    pageRect.moveCenter(xform.resultingRect().center());
  } else {
    pageRect = xform.resultingRect();
  }
  if (!xform.resultingRect().intersected(pageRect).isValid()) {
    pageRect = xform.resultingRect();
  }
  pageRect = pageRect.intersected(xform.resultingRect());
  contentRect = pageRect;
}

// As page_split::Task::process() with stored parameters.
void splitPage(const Steps& steps, const PageInfo& page, ImageTransformation& xform) {
  const page_split::Settings::Record record = steps.pageSplit.getPageRecord(page.imageId());
  const page_split::Params* params = record.params();
  // Without a usable split line the task finds one on the image.
  if (!params || !page_split::Utils::everyPageHasArea(params->pageLayout(), xform)) {
    return;
  }
  const page_split::Dependencies deps(xform.origRect().size().toSize(), xform.preRotation(), xform.origPreCropArea(),
                                      record.combinedLayoutType());
  page_split::PageLayout layout;
  if (deps.compatibleWith(*params)) {
    layout = params->pageLayout();
    page_split::PageLayoutAdapter::correctPageLayoutType(&layout);
  } else if (record.combinedLayoutType() != page_split::AUTO_LAYOUT_TYPE) {
    layout = page_split::PageLayoutAdapter::adaptPageLayout(params->pageLayout(), xform.resultingRect());
  } else {
    return;
  }
  xform.setPreCropArea(page_split::Utils::pageArea(layout, page.id().subPage(), xform.preCropArea()));
}

void processPage(Steps& steps, const PageInfo& page) {
  const QSize imageSize = page.metadata().size();
  if (imageSize.isEmpty() || ImageSizeLimit::exceeds(imageSize)) {
    return;
  }
  loadDefaultSettings(steps, page);
  ImageTransformation xform(QRectF(QPointF(0, 0), imageSize), page.metadata().dpi());

  const fix_orientation::ImageTrim trim = steps.fixOrientation.getTrim(page.imageId());
  if (trim.enabled) {
    check(QRect(QPoint(0, 0), imageSize).contains(trim.toInnerRect(imageSize)),
          "the trimmed image lies inside the image");
  }
  fix_orientation::Utils::applyRotationAndTrim(xform, steps.fixOrientation.getRotationFor(page.imageId()), trim);
  checkGeometry(xform, "Fix Orientation leaves a finite geometry");

  splitPage(steps, page, xform);
  checkGeometry(xform, "Split Pages leaves a finite geometry");
  // As BlackOnWhiteEstimator and BackgroundColorCalculator check it; they throw otherwise.
  check(!xform.origPreCropArea().intersected(xform.origRect()).isEmpty(),
        "Split Pages leaves the page a part of the image");

  if (const std::unique_ptr<deskew::Params> params = steps.deskew.getPageParams(page.id())) {
    xform.setPostRotation(params->deskewAngle());
    xform.setPostOblique(params->obliqueAngle());
  }
  checkGeometry(xform, "Deskew leaves a finite geometry");

  QRectF pageRect = xform.resultingRect();
  QRectF contentRect = pageRect;
  if (const std::unique_ptr<select_content::Params> params = steps.selectContent.getPageParams(page.id())) {
    selectContentBoxes(*params, xform, pageRect, contentRect);
  }

  // As page_layout::Task::process().
  const QSizeF contentSizeMm(page_layout::Utils::calcRectSizeMM(xform, contentRect));
  if (steps.pageLayout.isPageAutoMarginsEnabled(page.id())) {
    steps.pageLayout.setHardMarginsMM(page.id(), page_layout::Utils::calcMarginsMM(xform, pageRect, contentRect));
  }
  QSizeF aggHardSizeAfter;
  const page_layout::Params params(steps.pageLayout.updateContentSizeAndGetParams(
      page.id(), pageRect, contentRect, contentSizeMm, nullptr, &aggHardSizeAfter));
  const QRectF adaptedContentRect(page_layout::Utils::adaptContentRect(xform, contentRect));
  const QPolygonF contentRectPhys(xform.transformBack().map(adaptedContentRect));
  double scaleFactor = 1.0;
  const QPolygonF pageRectPhys(page_layout::Utils::calcPageRectPhys(
      xform, contentRectPhys, params, aggHardSizeAfter, steps.pageLayout.outputSizeOptions(), &scaleFactor));
  xform.setPostCropArea(page_layout::Utils::shiftToRoundedOrigin(xform.transform().map(pageRectPhys)));
  if (scaleFactor != 1.0) {
    xform.setPostScaleFactor(scaleFactor);
  }
  checkGeometry(xform, "Margins leaves a finite geometry");

  // As output::Task::process() up to the size of the output.
  xform.postScaleToDpi(steps.output.getParams(page.id()).outputDpi());
  checkGeometry(xform, "the output resolution leaves a finite geometry");
  check(xform.resultingRect().toRect().topLeft() == QPoint(0, 0), "the output starts at 0, 0");
  const output::OutputGenerator generator(xform, contentRectPhys);
  (void) generator.outputImageSize();
}
}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  QDomDocument doc;
  if (!doc.setContent(fuzz::bytes(data, size))) {
    return 0;
  }
  const ProjectReader reader(doc, QDir::temp().filePath("fuzz/project.ScanTailor"));
  // The program asks for the resolutions to be fixed before it processes anything.
  if (!reader.success() || !reader.pages()->validateDpis()) {
    return 0;
  }

  const QDomElement filtersEl = doc.documentElement().namedItem("filters").toElement();
  Steps steps;
  const QDomElement fixOrientationEl = filtersEl.namedItem("fix-orientation").toElement();
  steps.fixOrientation.load(reader, fixOrientationEl);
  steps.fixOrientationImageSettings.load(reader, fixOrientationEl.namedItem("image-settings").toElement());
  steps.pageSplit.load(reader, filtersEl.namedItem("page-split").toElement());
  const QDomElement deskewEl = filtersEl.namedItem("deskew").toElement();
  steps.deskew.load(reader, deskewEl);
  steps.deskewImageSettings.load(reader, deskewEl.namedItem("image-settings").toElement());
  steps.selectContent.load(reader, filtersEl.namedItem("select-content").toElement());
  steps.pageLayout.load(reader, filtersEl.namedItem("page-layout").toElement());
  steps.output.load(reader, filtersEl.namedItem("output").toElement());
  if (const std::unique_ptr<DefaultParams> startValues = reader.readDefaultParams()) {
    steps.defaults = *startValues;
  }
  if (filtersEl.namedItem("page-layout").isNull()) {
    steps.pageLayout.setOutputSizeOptions(page_layout::Utils::buildDefaultOutputSizeOptions(steps.defaults));
  }

  const PageSequence pages = reader.pages()->toPageSequence(PAGE_VIEW);
  try {
    for (size_t i = 0; (i < pages.numPages()) && (i < MAX_PAGES); ++i) {
      processPage(steps, pages.pageAt(i));
    }
  } catch (const std::bad_alloc&) {
    // The program hands it to its out-of-memory handler.
  }
  return 0;
}
