// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <ImageId.h>
#include <PageId.h>
#include <PropertySet.h>
#include <RelinkedKeys.h>
#include <filters/output/Settings.h>
#include <zones/SerializableSpline.h>
#include <zones/Zone.h>
#include <zones/ZoneSet.h>

#include <QDomDocument>
#include <QDomElement>
#include <QPolygonF>
#include <QRectF>
#include <boost/test/unit_test.hpp>
#include <map>
#include <memory>
#include <unordered_map>

#include "TestHelpers.h"

BOOST_AUTO_TEST_SUITE(RelinkedKeysTestSuite)

BOOST_AUTO_TEST_CASE(test_page_keys_follow_the_moved_file) {
  const ImageId scan("/a/scan.tif", 2);
  const ImageId other("/b/other.tif");
  const std::map<PageId, int> perPage{
      {PageId(scan, PageId::LEFT_PAGE), 1}, {PageId(scan, PageId::RIGHT_PAGE), 2}, {PageId(other), 3}};

  const std::map<PageId, int> relinked
      = relinkedKeys(perPage, Tests::RenamingRelinker("/a/scan.tif", "/c/scan_new.tif"));

  // The page number in the file and the half of the page stay; only the path changes.
  const ImageId moved("/c/scan_new.tif", 2);
  const std::map<PageId, int> expected{
      {PageId(moved, PageId::LEFT_PAGE), 1}, {PageId(moved, PageId::RIGHT_PAGE), 2}, {PageId(other), 3}};
  BOOST_TEST((relinked == expected));
}

BOOST_AUTO_TEST_CASE(test_image_keys_follow_the_moved_file) {
  const std::unordered_map<ImageId, int> perImage{{ImageId("/a/scan.tif"), 1}, {ImageId("/b/other.tif"), 2}};

  const std::unordered_map<ImageId, int> relinked
      = relinkedKeys(perImage, Tests::RenamingRelinker("/a/scan.tif", "/c/scan_new.tif"));

  const std::unordered_map<ImageId, int> expected{{ImageId("/c/scan_new.tif"), 1}, {ImageId("/b/other.tif"), 2}};
  BOOST_TEST((relinked == expected));
}


BOOST_AUTO_TEST_CASE(test_output_settings_relink_every_table) {
  const PageId page(ImageId("/a/scan.tif"));
  const PageId moved(ImageId("/c/scan_new.tif"));
  ZoneSet zones;
  zones.add(Zone(SerializableSpline(QPolygonF(QRectF(10, 10, 50, 50))), PropertySet()));
  output::OutputProcessingParams processingParams;
  processingParams.setBlackOnWhiteSetManually(true);
  // The stored output of the page, read from XML: only its despeckle level is set.
  QDomDocument doc;
  QDomElement outputEl(doc.createElement("output"));
  QDomElement imageEl(doc.createElement("image"));
  imageEl.setAttribute("despeckleLevel", 1.5);
  outputEl.appendChild(imageEl);

  output::Settings settings;
  settings.setDespeckleLevel(page, 2.5);
  settings.setPictureZones(page, zones);
  settings.setFillZones(page, zones);
  settings.setOutputProcessingParams(page, processingParams);
  settings.setOutputParams(page, output::OutputParams(outputEl));

  settings.performRelinking(Tests::RenamingRelinker("/a/scan.tif", "/c/scan_new.tif"));

  BOOST_TEST(settings.isParamsNull(page));
  BOOST_TEST(settings.getParams(moved).despeckleLevel() == 2.5);
  BOOST_TEST(settings.pictureZonesForPage(page).empty());
  BOOST_TEST(!settings.pictureZonesForPage(moved).empty());
  BOOST_TEST(!settings.fillZonesForPage(moved).empty());
  BOOST_TEST(settings.getOutputProcessingParams(moved).isBlackOnWhiteSetManually());
  // Without the stored output, the moved page would count as out of date and be processed again.
  BOOST_TEST(!settings.getOutputParams(page));
  const std::unique_ptr<output::OutputParams> movedOutput(settings.getOutputParams(moved));
  BOOST_REQUIRE(movedOutput);
  BOOST_TEST(movedOutput->outputImageParams().despeckleLevel() == 1.5);
}

BOOST_AUTO_TEST_SUITE_END()
