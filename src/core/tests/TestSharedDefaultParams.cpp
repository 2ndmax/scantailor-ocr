#include <DefaultParams.h>
#include <ProjectPages.h>
#include <StageSequence.h>

#include <filters/deskew/SkewDetection.h>
#include <filters/select_content/DetectionOptions.h>

#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(SharedDefaultParamsSuite)

BOOST_AUTO_TEST_CASE(start_new_project_propagates_deskew_and_select_content_params) {
  DefaultParams startValues;
  DefaultParams::DeskewParams deskewParams;
  deskewParams.setAutoOblique(true);
  deskewParams.setDeskewAngleDeg(7.0);
  deskewParams.setDetection(deskew::DETECT_TOP_EDGE);
  startValues.setDeskewParams(deskewParams);

  DefaultParams::SelectContentParams selectParams;
  selectParams.setContentDetectEnabled(false);
  selectParams.setDetectionOptions(select_content::DetectionOptions(42, 7, false));
  startValues.setSelectContentParams(selectParams);

  auto pages = std::make_shared<ProjectPages>();
  StageSequence stages(pages);
  stages.startNewProject(startValues);

  BOOST_CHECK(stages.deskewFilter()->defaultParams()->isAutoOblique());
  BOOST_CHECK_CLOSE(stages.deskewFilter()->defaultParams()->getDeskewAngleDeg(), 7.0, 1e-9);
  BOOST_CHECK(stages.deskewFilter()->defaultParams()->getDetection() == deskew::DETECT_TOP_EDGE);
  BOOST_CHECK(!stages.selectContentFilter()->defaultParams()->isContentDetectEnabled());
  BOOST_CHECK_EQUAL(stages.selectContentFilter()->defaultParams()->getDetectionOptions().fillFactor(), 42);
  BOOST_CHECK_EQUAL(stages.selectContentFilter()->defaultParams()->getDetectionOptions().borderTolerance(), 7);
}

BOOST_AUTO_TEST_CASE(profile_change_propagates_deskew_and_select_content_params) {
  DefaultParams startValues;
  auto pages = std::make_shared<ProjectPages>();
  StageSequence stages(pages);
  stages.startNewProject(startValues);

  // Mutate the shared default params (as a profile change would).
  DefaultParams::DeskewParams updated = stages.defaultParams()->getDeskewParams();
  updated.setAutoOblique(true);
  updated.setDeskewAngleDeg(9.0);
  updated.setDetection(deskew::DETECT_TOP_EDGE);
  stages.defaultParams()->setDeskewParams(updated);

  DefaultParams::SelectContentParams updatedSel = stages.defaultParams()->getSelectContentParams();
  updatedSel.setContentDetectEnabled(false);
  updatedSel.setDetectionOptions(select_content::DetectionOptions(13, 31, false));
  stages.defaultParams()->setSelectContentParams(updatedSel);

  BOOST_CHECK(stages.deskewFilter()->defaultParams()->isAutoOblique());
  BOOST_CHECK_CLOSE(stages.deskewFilter()->defaultParams()->getDeskewAngleDeg(), 9.0, 1e-9);
  BOOST_CHECK(stages.deskewFilter()->defaultParams()->getDetection() == deskew::DETECT_TOP_EDGE);
  BOOST_CHECK(!stages.selectContentFilter()->defaultParams()->isContentDetectEnabled());
  BOOST_CHECK_EQUAL(stages.selectContentFilter()->defaultParams()->getDetectionOptions().fillFactor(), 13);
  BOOST_CHECK_EQUAL(stages.selectContentFilter()->defaultParams()->getDetectionOptions().borderTolerance(), 31);
}

BOOST_AUTO_TEST_SUITE_END()