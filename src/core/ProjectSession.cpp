// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectSession.h"

#include <QDir>

#include "DefaultParams.h"
#include "FileNameDisambiguator.h"
#include "PageSelectionAccessor.h"
#include "PageSequence.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "StageSequence.h"
#include "Utils.h"

namespace {
/** What a new and a loaded project have in common. \p reader is null for a new project. */
ProjectSession setUp(const std::shared_ptr<ProjectPages>& pages,
                     const QString& outDir,
                     const QString& inputDir,
                     const ProjectReader* reader,
                     const DefaultParams& defaults,
                     const PageSelectionAccessor& pageSelectionAccessor) {
  ProjectSession session;
  session.pages = pages;

  if (pages->numImages() == 0) {
    if (!inputDir.isEmpty()) {
      session.emptyProjectInputDir = inputDir;
    } else if (reader && !reader->directories().empty()) {
      session.emptyProjectInputDir = reader->directories().front();
    }
  }

  if (!outDir.isEmpty() && !QDir(outDir).exists()
      && project_files::isProjectInPlace(*pages, outDir, session.emptyProjectInputDir)) {
    // The output folder was deleted, e.g. to output everything again. The project is
    // where it was, so the folder is created again rather than asking for relinking.
    QDir().mkdir(outDir);
  }
  if (!outDir.isEmpty()) {
    core::Utils::maybeCreateCacheDir(outDir);
  }

  if (reader) {
    session.selectedPage = reader->selectedPage();
  }

  const std::shared_ptr<FileNameDisambiguator> disambiguator
      = reader ? reader->namingDisambiguator() : std::make_shared<FileNameDisambiguator>();
  session.outFileNameGen = OutputFileNameGenerator(disambiguator, outDir, pages->layoutDirection());
  for (const PageInfo& page : pages->toPageSequence(IMAGE_VIEW)) {
    disambiguator->registerFile(page.imageId().filePath());
  }

  session.stages = std::make_shared<StageSequence>(pages, pageSelectionAccessor);
  // New projects start with the default parameters; older projects take what they lack from them.
  if (reader) {
    session.stages->loadProject(*reader, defaults);
  } else {
    session.stages->startNewProject(defaults);
  }

  // Thumbnails are stored relative to the output directory.
  if (!outDir.isEmpty()) {
    session.thumbnailCache = core::Utils::createThumbnailCache(outDir);
  }
  return session;
}
}  // namespace

ProjectSession ProjectSession::createNew(const std::shared_ptr<ProjectPages>& pages,
                                         const QString& outDir,
                                         const QString& inputDir,
                                         const DefaultParams& startValues,
                                         const PageSelectionAccessor& pageSelectionAccessor) {
  return setUp(pages, outDir, inputDir, nullptr, startValues, pageSelectionAccessor);
}

ProjectSession ProjectSession::load(const ProjectReader& reader,
                                    const DefaultParams& fallback,
                                    const PageSelectionAccessor& pageSelectionAccessor) {
  return setUp(reader.pages(), reader.outputDirectory(), QString(), &reader, fallback, pageSelectionAccessor);
}

bool ProjectSession::write(const QString& projectFile) const {
  return ProjectWriter(pages, selectedPage, outFileNameGen, emptyProjectInputDir).write(projectFile, *stages);
}
