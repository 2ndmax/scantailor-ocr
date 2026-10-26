// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef TST_PROJECT_XML_H
#define TST_PROJECT_XML_H

#include <AbstractFilter.h>
#include <DefaultParams.h>
#include <Dpi.h>
#include <FileNameDisambiguator.h>
#include <ImageInfo.h>
#include <OutputFileNameGenerator.h>
#include <ProcessingOptions.h>
#include <ProjectPages.h>
#include <ProjectReader.h>
#include <ProjectWriter.h>
#include <SelectedPage.h>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <functional>
#include <memory>
#include <vector>

/** Helpers for the tests of how filter settings are saved in a project file. */
namespace ProjectXml {
/** Reads an XML file. Returns false if the file cannot be read or is not well-formed XML. */
inline bool readDocument(const QString& filePath, QDomDocument& doc) {
  QFile file(filePath);
  return file.open(QIODevice::ReadOnly) && static_cast<bool>(doc.setContent(&file));
}

namespace detail {
inline void saveAndReload(const QString& filterName,
                          const std::function<QDomElement(QDomDocument& doc, const QString& imageId)>& makeFilterEl,
                          const std::function<std::shared_ptr<AbstractFilter>(const ProjectReader&)>& makeFilter,
                          QDomElement& savedFilterEl) {
  QTemporaryDir tempDir;
  QVERIFY(tempDir.isValid());
  const QDir dir(tempDir.path());
  const QString firstFile = dir.absoluteFilePath("first.ScanTailor");
  const QString secondFile = dir.absoluteFilePath("second.ScanTailor");
  const OutputFileNameGenerator outGen(std::make_shared<FileNameDisambiguator>(), dir.absoluteFilePath("out"),
                                       Qt::LeftToRight);

  const ImageMetadata metadata(QSize(2000, 3000), Dpi(300, 300));
  auto pages = std::make_shared<ProjectPages>(
      std::vector<ImageInfo>{ImageInfo(ImageId(dir.absoluteFilePath("scan.png"), 0), metadata, 1, false, false)},
      Qt::LeftToRight);
  QVERIFY(ProjectWriter(pages, SelectedPage(), outGen).write(firstFile, {}, ProcessingOptions(), DefaultParams()));

  QDomDocument doc;
  QVERIFY(readDocument(firstFile, doc));
  QDomElement projectEl = doc.documentElement();
  const QString imageId = projectEl.namedItem("images").firstChildElement("image").attribute("id");
  QVERIFY(!imageId.isEmpty());
  QDomElement filtersEl = projectEl.namedItem("filters").toElement();
  if (filtersEl.isNull()) {
    filtersEl = doc.createElement("filters");
    projectEl.appendChild(filtersEl);
  }
  filtersEl.appendChild(makeFilterEl(doc, imageId));

  const ProjectReader reader(doc, firstFile);
  QVERIFY(reader.success());
  const std::shared_ptr<AbstractFilter> filter = makeFilter(reader);
  reader.readFilterSettings({filter});
  QVERIFY(ProjectWriter(reader.pages(), SelectedPage(), outGen)
              .write(secondFile, {filter}, ProcessingOptions(), DefaultParams()));

  QDomDocument saved;
  QVERIFY(readDocument(secondFile, saved));
  savedFilterEl = saved.documentElement().namedItem("filters").namedItem(filterName).toElement();
}
}  // namespace detail

/**
 * Saves a project with one image, adds the element of a filter for that image to its
 * <filters>, loads the project with that filter and saves it again.
 *
 * \param filterName The name of the filter's element, e.g. "page-split".
 * \param makeFilterEl Builds that element for the image with the given id.
 * \param makeFilter Creates the filter that reads and writes the element.
 * \return The filter's element in the saved project, or a null element if a step
 *         failed. The failed step is reported as a test failure.
 */
inline QDomElement saveAndReload(
    const QString& filterName,
    const std::function<QDomElement(QDomDocument& doc, const QString& imageId)>& makeFilterEl,
    const std::function<std::shared_ptr<AbstractFilter>(const ProjectReader&)>& makeFilter) {
  // QVERIFY only works in functions without a return value.
  QDomElement savedFilterEl;
  detail::saveAndReload(filterName, makeFilterEl, makeFilter, savedFilterEl);
  return savedFilterEl;
}
}  // namespace ProjectXml

#endif  // TST_PROJECT_XML_H
