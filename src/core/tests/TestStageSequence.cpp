// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DefaultParams.h>
#include <Dpi.h>
#include <Dpm.h>
#include <ImageFileInfo.h>
#include <ImageMetadata.h>
#include <PageSelectionAccessor.h>
#include <ProjectPages.h>
#include <ProjectSession.h>
#include <StageSequence.h>

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>

BOOST_AUTO_TEST_SUITE(StageSequenceTestSuite)

// core_tests has no QApplication, only a QCoreApplication, like a command line tool without a
// display: creating a widget would end the program. The steps create their options panels only
// when the GUI asks for them, so a project can be set up and processed without one.
BOOST_AUTO_TEST_CASE(test_process_all_without_gui) {
  QTemporaryDir dir;
  BOOST_REQUIRE(dir.isValid());

  // A white page with a block of "text" lines; drawing text itself would need a QGuiApplication.
  const int dpi = 300;
  QImage page(1200, 1600, QImage::Format_RGB32);
  page.fill(Qt::white);
  {
    QPainter painter(&page);
    for (int y = 300; y < 1300; y += 60) {
      painter.fillRect(QRect(200, y, 800, 25), Qt::black);
    }
  }
  const Dpm dpm(Dpi(dpi, dpi));
  page.setDotsPerMeterX(dpm.horizontal());
  page.setDotsPerMeterY(dpm.vertical());
  const QString pagePath = QDir(dir.path()).absoluteFilePath("page.png");
  BOOST_REQUIRE(page.save(pagePath));
  const std::vector<ImageFileInfo> files{
      ImageFileInfo(QFileInfo(pagePath), {ImageMetadata(page.size(), Dpi(dpi, dpi))})};

  const QString outDir = QDir(dir.path()).absoluteFilePath("out");
  BOOST_REQUIRE(QDir().mkpath(outDir));
  const DefaultParams startValues;
  const ProjectSession session = ProjectSession::createNew(ProjectPages::createNew(files, startValues), outDir,
                                                           QString(), startValues, PageSelectionAccessor(nullptr));

  // Without thumbnails: they are pixmaps, which also need a GUI.
  const StageSequence::ProcessAllResult result = session.stages->processAll(nullptr, session.outFileNameGen);
  BOOST_CHECK(result.failures.empty());
  BOOST_CHECK(result.output);
  BOOST_CHECK_EQUAL(QDir(outDir).entryList({"*.tif"}, QDir::Files).size(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
