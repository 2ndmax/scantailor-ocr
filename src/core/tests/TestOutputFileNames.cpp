// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <FileNameDisambiguator.h>
#include <ImageId.h>
#include <OutputFileNameGenerator.h>
#include <PageId.h>

#include <QDomDocument>
#include <QStringList>
#include <boost/test/unit_test.hpp>
#include <memory>
#include <vector>

#include "TestHelpers.h"

namespace Tests {
namespace {
/** Registers the pages' images as the main window does, and returns their output names. */
QStringList outputNames(const std::shared_ptr<FileNameDisambiguator>& disambiguator, const std::vector<PageId>& pages) {
  const OutputFileNameGenerator generator(disambiguator, "/out", Qt::LeftToRight);
  QStringList names;
  for (const PageId& page : pages) {
    disambiguator->registerFile(page.imageId().filePath());
  }
  for (const PageId& page : pages) {
    names.push_back(generator.fileNameFor(page));
  }
  return names;
}

/** True if no two names lead to the same file, also on a file system that ignores case. */
bool allDistinct(const QStringList& names) {
  for (int i = 0; i < names.size(); ++i) {
    for (int j = i + 1; j < names.size(); ++j) {
      if (names[i].compare(names[j], Qt::CaseInsensitive) == 0) {
        BOOST_TEST_MESSAGE("same output name: " << names[i].toStdString() << " / " << names[j].toStdString());
        return false;
      }
    }
  }
  return true;
}

PageId page(const QString& path, const int pageNumber = 0, const PageId::SubPage subPage = PageId::SINGLE_PAGE) {
  return PageId(ImageId(path, pageNumber), subPage);
}
}  // namespace

BOOST_AUTO_TEST_SUITE(OutputFileNamesTestSuite)

BOOST_AUTO_TEST_CASE(test_same_base_name_with_other_extension) {
  const auto disambiguator = std::make_shared<FileNameDisambiguator>();
  BOOST_TEST(allDistinct(outputNames(disambiguator, {page("/in/a.tif"), page("/in/a.png"), page("/in/a")})));
}

BOOST_AUTO_TEST_CASE(test_names_differing_only_in_case) {
  const auto disambiguator = std::make_shared<FileNameDisambiguator>();
  BOOST_TEST(allDistinct(outputNames(disambiguator, {page("/scan1/Scan.tif"), page("/scan2/scan.tif")})));
}

BOOST_AUTO_TEST_CASE(test_names_that_look_like_a_label_or_suffix) {
  const auto disambiguator = std::make_shared<FileNameDisambiguator>();
  BOOST_TEST(allDistinct(outputNames(disambiguator, {page("/y/k.tif"), page("/z/k.tif"), page("/x/k(1).tif")})));

  const auto other = std::make_shared<FileNameDisambiguator>();
  BOOST_TEST(allDistinct(outputNames(other, {page("/in/x.tif", 1), page("/in/x_page0001.tif"),
                                             page("/in/y.tif", 0, PageId::LEFT_PAGE), page("/in/y_1L.tif")})));
}

BOOST_AUTO_TEST_CASE(test_labels_stay_after_loading_and_adding_files) {
  // The number of a page must not change when other files are added later.
  const auto disambiguator = std::make_shared<FileNameDisambiguator>();
  disambiguator->registerFile("/a/scan.tif");
  disambiguator->registerFile("/b/scan.tif");
  QDomDocument doc;
  const QDomElement el(disambiguator->toXml(doc, "disambiguation"));
  FileNameDisambiguator loaded(el);
  BOOST_TEST(loaded.getLabel("/a/scan.tif") == 0);
  BOOST_TEST(loaded.getLabel("/b/scan.tif") == 1);
  loaded.registerFile("/0/scan.tif");
  BOOST_TEST(loaded.getLabel("/a/scan.tif") == 0);
  BOOST_TEST(loaded.getLabel("/b/scan.tif") == 1);
  BOOST_TEST(loaded.getLabel("/0/scan.tif") == 2);
}

BOOST_AUTO_TEST_CASE(test_relinking_to_a_name_with_the_same_label) {
  const auto disambiguator = std::make_shared<FileNameDisambiguator>();
  const std::vector<PageId> before{page("/c/scan_new.tif"), page("/b/scan_new.tif"), page("/c/scan.tif"),
                                   page("/a/scan.tif")};
  outputNames(disambiguator, before);
  BOOST_TEST(disambiguator->getLabel("/a/scan.tif") == 1);
  BOOST_TEST(disambiguator->getLabel("/b/scan_new.tif") == 1);

  disambiguator->performRelinking(RenamingRelinker("/a/scan.tif", "/a/scan_new.tif"));
  const OutputFileNameGenerator generator(disambiguator, "/out", Qt::LeftToRight);
  const QStringList names{generator.fileNameFor(page("/c/scan_new.tif")),
                          generator.fileNameFor(page("/b/scan_new.tif")), generator.fileNameFor(page("/c/scan.tif")),
                          generator.fileNameFor(page("/a/scan_new.tif"))};
  BOOST_TEST(allDistinct(names));
  // The files that weren't moved keep their names.
  BOOST_TEST(disambiguator->getLabel("/b/scan_new.tif") == 1);
  BOOST_TEST(disambiguator->getLabel("/c/scan_new.tif") == 0);
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
