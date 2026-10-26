// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageId.h>
#include <Margins.h>
#include <PageId.h>
#include <filters/page_layout/Alignment.h>
#include <filters/page_layout/Params.h>
#include <filters/page_layout/Settings.h>

#include <QRectF>
#include <QSizeF>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <sstream>
#include <string>

using namespace page_layout;

namespace {
const PageId kPage(ImageId("/a/scan.tif"));

/** All values of a page's params in one line, so that one check compares all of them. */
std::string describe(const Settings& settings, const PageId& page) {
  const std::unique_ptr<Params> params = settings.getPageParams(page);
  if (!params) {
    return "none";
  }
  const Margins& m = params->hardMarginsMM();
  const QRectF& p = params->pageRect();
  const QRectF& c = params->contentRect();
  const QSizeF& s = params->contentSizeMM();
  std::ostringstream out;
  out << "margins " << m.left() << ' ' << m.top() << ' ' << m.right() << ' ' << m.bottom() << "; page " << p.x() << ' '
      << p.y() << ' ' << p.width() << ' ' << p.height() << "; content " << c.x() << ' ' << c.y() << ' ' << c.width()
      << ' ' << c.height() << "; size " << s.width() << ' ' << s.height() << "; alignment "
      << params->alignment().vertical() << ' ' << params->alignment().horizontal() << "; auto "
      << params->isAutoMarginsEnabled();
  return out.str();
}

// The defaults of a new page: margins 10/5/10/5 mm, no rects, an invalid size, top/center.
const std::string kNoRects = "page 0 0 0 0; content 0 0 0 0; size -1 -1";
}  // namespace

BOOST_AUTO_TEST_SUITE(PageLayoutSettingsTestSuite)

BOOST_AUTO_TEST_CASE(test_each_setter_creates_a_page_with_the_defaults) {
  Settings margins;
  margins.setHardMarginsMM(kPage, Margins(1, 2, 3, 4));
  BOOST_TEST(describe(margins, kPage) == "margins 1 2 3 4; " + kNoRects + "; alignment 0 1; auto 0");

  Settings alignment;
  alignment.setPageAlignment(kPage, Alignment(Alignment::BOTTOM, Alignment::RIGHT));
  BOOST_TEST(describe(alignment, kPage) == "margins 10 5 10 5; " + kNoRects + "; alignment 2 2; auto 0");

  Settings size;
  BOOST_TEST((size.setContentSizeMM(kPage, QSizeF(100, 200)) == Settings::AGGREGATE_SIZE_CHANGED));
  BOOST_TEST(describe(size, kPage)
             == "margins 10 5 10 5; page 0 0 0 0; content 0 0 0 0; size 100 200; alignment 0 1; auto 0");

  Settings autoMargins;
  autoMargins.setPageAutoMarginsEnabled(kPage, true);
  BOOST_TEST(describe(autoMargins, kPage) == "margins 10 5 10 5; " + kNoRects + "; alignment 0 1; auto 1");

  Settings update;
  QSizeF before;
  QSizeF after;
  const Params params = update.updateContentSizeAndGetParams(kPage, QRectF(0, 0, 1000, 2000), QRectF(10, 20, 30, 40),
                                                             QSizeF(50, 60), &before, &after);
  BOOST_TEST(describe(update, kPage)
             == "margins 10 5 10 5; page 0 0 1000 2000; content 10 20 30 40; size 50 60; alignment 0 1; auto 0");
  BOOST_TEST((params.contentSizeMM() == QSizeF(50, 60)));
  BOOST_TEST((before == QSizeF(0, 0)));
  BOOST_TEST((after == QSizeF(70, 70)));
}

BOOST_AUTO_TEST_CASE(test_each_setter_keeps_the_other_values_of_a_page) {
  Settings settings;
  settings.updateContentSizeAndGetParams(kPage, QRectF(0, 0, 1000, 2000), QRectF(10, 20, 30, 40), QSizeF(50, 60));
  settings.setPageAutoMarginsEnabled(kPage, true);
  settings.setHardMarginsMM(kPage, Margins(1, 2, 3, 4));
  BOOST_TEST((settings.setPageAlignment(kPage, Alignment(Alignment::BOTTOM, Alignment::RIGHT))
              == Settings::AGGREGATE_SIZE_UNCHANGED));
  BOOST_TEST(describe(settings, kPage)
             == "margins 1 2 3 4; page 0 0 1000 2000; content 10 20 30 40; size 50 60; alignment 2 2; auto 1");

  // A new content size keeps the page rect, but not the content rect.
  BOOST_TEST((settings.setContentSizeMM(kPage, QSizeF(70, 80)) == Settings::AGGREGATE_SIZE_CHANGED));
  BOOST_TEST(describe(settings, kPage)
             == "margins 1 2 3 4; page 0 0 1000 2000; content 0 0 0 0; size 70 80; alignment 2 2; auto 1");

  settings.invalidateContentSize(kPage);
  BOOST_TEST(describe(settings, kPage)
             == "margins 1 2 3 4; page 0 0 1000 2000; content 0 0 0 0; size -1 -1; alignment 2 2; auto 1");
}

BOOST_AUTO_TEST_SUITE_END()
