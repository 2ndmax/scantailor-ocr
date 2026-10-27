// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BeforeOrAfter.h>
#include <Dpi.h>
#include <ImageId.h>
#include <ImageInfo.h>
#include <ImageMetadata.h>
#include <OrthogonalRotation.h>
#include <PageId.h>
#include <PageInfo.h>
#include <PageSequence.h>
#include <PageView.h>
#include <ProjectPages.h>

#include <QFileInfo>
#include <boost/test/unit_test.hpp>
#include <string>

namespace Tests {
namespace {
const ImageMetadata kPortrait(QSize(1000, 2000), Dpi(300, 300));
const ImageMetadata kLandscape(QSize(2000, 1000), Dpi(300, 300));

ImageInfo image(const char* name,
                const ImageMetadata& metadata,
                const int numSubPages,
                const bool leftRemoved = false,
                const bool rightRemoved = false) {
  return ImageInfo(ImageId(QString("/in/") + name + ".png"), metadata, numSubPages, leftRemoved, rightRemoved);
}

ImageId idOf(const char* name) {
  return ImageId(QString("/in/") + name + ".png");
}

/** The pages in order, as "name:S|L|R" plus "l"/"r" for a removed left/right half. */
std::string describe(const ProjectPages& pages) {
  std::string out;
  for (const PageInfo& page : pages.toPageSequence(PAGE_VIEW)) {
    if (!out.empty()) {
      out += ' ';
    }
    out += QFileInfo(page.imageId().filePath()).baseName().toStdString() + ':';
    switch (page.id().subPage()) {
      case PageId::SINGLE_PAGE:
        out += 'S';
        break;
      case PageId::LEFT_PAGE:
        out += 'L';
        break;
      case PageId::RIGHT_PAGE:
        out += 'R';
        break;
    }
    if (page.leftHalfRemoved()) {
      out += 'l';
    }
    if (page.rightHalfRemoved()) {
      out += 'r';
    }
  }
  return out;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectPagesLayoutTestSuite)

BOOST_AUTO_TEST_CASE(new_images_follow_the_rules_of_removed_halves) {
  // b keeps its removed left half; d (three pages) is not taken; e (both halves removed) and
  // f (two pages, a removed half) get their halves back.
  ProjectPages pages({image("a", kPortrait, 1), image("b", kLandscape, 1, true), image("c", kLandscape, 2),
                      image("d", kLandscape, 3), image("e", kPortrait, 1, true, true), image("f", kLandscape, 2, true)},
                     Qt::LeftToRight);
  BOOST_TEST(describe(pages) == "a:S b:Rl c:L c:R e:S f:L f:R");

  BOOST_TEST(pages.insertImage(image("g", kPortrait, 1, true, true), BEFORE, ImageId(), PAGE_VIEW).size() == 1);
  BOOST_TEST(pages.insertImage(image("h", kPortrait, 3), BEFORE, ImageId(), PAGE_VIEW).empty());
  BOOST_TEST(describe(pages) == "a:S b:Rl c:L c:R e:S f:L f:R g:S");
}

BOOST_AUTO_TEST_CASE(layout_changes_keep_removed_halves_and_signal_only_real_changes) {
  ProjectPages pages({image("a", kPortrait, 1), image("b", kLandscape, 1, true), image("c", kLandscape, 2)},
                     Qt::LeftToRight);
  int modifiedCount = 0;
  QObject::connect(&pages, &ProjectPages::modified, [&]() { ++modifiedCount; });

  pages.setLayoutTypeFor(idOf("a"), ProjectPages::TWO_PAGE_LAYOUT);
  BOOST_TEST(describe(pages) == "a:L a:R b:Rl c:L c:R");
  BOOST_TEST(modifiedCount == 1);

  // With a removed half, two pages mean one page: nothing changes.
  pages.setLayoutTypeFor(idOf("b"), ProjectPages::TWO_PAGE_LAYOUT);
  pages.setLayoutTypeFor(idOf("unknown"), ProjectPages::TWO_PAGE_LAYOUT);
  BOOST_TEST(modifiedCount == 1);

  pages.setLayoutTypeForAllPages(ProjectPages::ONE_PAGE_LAYOUT);
  BOOST_TEST(describe(pages) == "a:S b:Rl c:S");
  BOOST_TEST(modifiedCount == 2);
  pages.setLayoutTypeForAllPages(ProjectPages::ONE_PAGE_LAYOUT);
  BOOST_TEST(modifiedCount == 2);

  // Landscape images are advised two pages, portrait ones one.
  pages.autoSetLayoutTypeFor(idOf("c"), OrthogonalRotation());
  BOOST_TEST(describe(pages) == "a:S b:Rl c:L c:R");
  BOOST_TEST(modifiedCount == 3);
  pages.autoSetLayoutTypeFor(idOf("a"), OrthogonalRotation());
  pages.autoSetLayoutTypeFor(idOf("b"), OrthogonalRotation());
  BOOST_TEST(modifiedCount == 3);

  OrthogonalRotation quarterTurn;
  quarterTurn.nextClockwiseDirection();
  pages.autoSetLayoutTypeFor(idOf("c"), quarterTurn);
  BOOST_TEST(describe(pages) == "a:S b:Rl c:S");
  BOOST_TEST(modifiedCount == 4);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
