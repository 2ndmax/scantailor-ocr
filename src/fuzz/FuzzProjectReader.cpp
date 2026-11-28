// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

// F2: reading a project file, without the settings of the filters (their objects need the GUI).
// Seeds: corpus/project, dictionary: project.dict.
//
// ProjectReader only works with the paths as text and opens no file, so the paths in the
// input can't reach real files. The project file path is made up, no file is created there.

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>
#include <vector>

#include "DefaultParams.h"
#include "FileNameDisambiguator.h"
#include "FuzzSetup.h"
#include "ImageFileInfo.h"
#include "OutputFileNameGenerator.h"
#include "PageSequence.h"
#include "ProcessingOptions.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size) {
  QDomDocument doc;
  if (!doc.setContent(fuzz::bytes(data, size))) {
    return 0;
  }
  const ProjectReader reader(doc, QDir::temp().filePath("fuzz/project.ScanTailor"));
  if (!reader.success()) {
    return 0;
  }

  // What the program does with a project it has read, apart from the filters.
  (void) reader.pages()->toPageSequence(IMAGE_VIEW);
  (void) reader.pages()->toPageSequence(PAGE_VIEW);
  (void) reader.pages()->validateDpis();
  (void) reader.pages()->toImageFileInfo();
  (void) reader.directories();

  // Written back as when the project is saved, including page/file lists via ProjectWriter.
  QDomDocument out;
  QDomElement projectEl = out.createElement("project");
  out.appendChild(projectEl);
  const ProcessingOptions options = reader.readProcessingOptions(ProcessingOptions());
  projectEl.appendChild(options.toXml(out, "processing-options"));
  const std::unique_ptr<DefaultParams> defaults = reader.readDefaultParams();
  if (defaults) {
    projectEl.appendChild(defaults->toXml(out, "default-params"));
  }
  projectEl.appendChild(reader.namingDisambiguator()->toXml(out, "file-name-disambiguation"));
  (void) out.toByteArray();

  static QTemporaryDir roundtripDir;
  if (roundtripDir.isValid()) {
    const QString roundtripPath = roundtripDir.filePath("roundtrip.ScanTailor");
    const OutputFileNameGenerator outGen(reader.namingDisambiguator(), reader.outputDirectory(),
                                         reader.pages()->layoutDirection());
    const ProjectWriter writer(reader.pages(), reader.selectedPage(), outGen);
    if (writer.write(roundtripPath, std::vector<ProjectWriter::FilterPtr>{}, options,
                     defaults ? *defaults : DefaultParams())) {
      QFile file(roundtripPath);
      QDomDocument rtDoc;
      if (file.open(QIODevice::ReadOnly) && rtDoc.setContent(&file)) {
        const ProjectReader rtReader(rtDoc, roundtripPath);
        (void) rtReader.pages()->toPageSequence(PAGE_VIEW);
      }
    }
  }
  return 0;
}
