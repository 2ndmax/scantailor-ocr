// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <FileNameDisambiguator.h>
#include <OutputFileNameGenerator.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace {
QDomDocument readDocument(const QString& filePath) {
  QDomDocument doc;
  QFile file(filePath);
  if (file.open(QIODevice::ReadOnly)) {
    doc.setContent(&file);
  }
  return doc;
}

QDomDocument parse(const QString& xml) {
  QDomDocument doc;
  doc.setContent(xml);
  return doc;
}
}  // namespace

BOOST_AUTO_TEST_SUITE(ProjectFileTestSuite)

BOOST_AUTO_TEST_CASE(test_empty_project_keeps_input_dir) {
  QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const QDir root(tempDir.path());
  BOOST_REQUIRE(root.mkdir("Buch"));
  const QString inputDir = root.absoluteFilePath("Buch");
  const QString projectFile = QDir(inputDir).absoluteFilePath("Buch.ScanTailor");

  auto pages = std::make_shared<ProjectPages>();
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(),
                                       QDir(inputDir).absoluteFilePath("out"), Qt::LeftToRight);
  const ProjectWriter writer(pages, SelectedPage(), outGen, inputDir);
  BOOST_REQUIRE(writer.write(projectFile, {}));

  const ProjectReader reader(readDocument(projectFile), projectFile);
  BOOST_REQUIRE(reader.success());
  BOOST_CHECK_EQUAL(reader.pages()->numImages(), 0);
  BOOST_CHECK(reader.outputDirectory() == QDir(inputDir).absoluteFilePath("out"));
  BOOST_REQUIRE_EQUAL(reader.directories().size(), 1u);
  BOOST_CHECK(QDir(reader.directories().front()) == QDir(inputDir));
}

BOOST_AUTO_TEST_CASE(test_empty_project_without_input_dir) {
  const ProjectReader reader(
      parse("<project version=\"4\" outputDirectory=\"/out\"><directories/><files/><images/><pages/></project>"));
  BOOST_REQUIRE(reader.success());
  BOOST_CHECK_EQUAL(reader.pages()->numImages(), 0);
  BOOST_CHECK(reader.directories().empty());
}

BOOST_AUTO_TEST_CASE(test_unreadable_images_are_an_error) {
  // The image refers to a file that is not listed, so it can't be read.
  const ProjectReader reader(
      parse("<project version=\"4\" outputDirectory=\"/out\"><directories/><files/>"
            "<images><image id=\"3\" subPages=\"1\" fileId=\"2\" fileImage=\"0\"/></images><pages/></project>"));
  BOOST_CHECK(!reader.success());
}

BOOST_AUTO_TEST_SUITE_END()
