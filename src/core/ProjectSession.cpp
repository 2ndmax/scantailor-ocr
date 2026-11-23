// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "ProjectSession.h"

#include <QDir>

#include "CoreUtils.h"
#include "DefaultParams.h"
#include "FileNameDisambiguator.h"
#include "PageSequence.h"
#include "ProjectFiles.h"
#include "ProjectPages.h"
#include "ProjectReader.h"
#include "ProjectWriter.h"
#include "StageSequence.h"

namespace {
/** What a new and a loaded project have in common. \p reader is null for a new project. */
ProjectSession setUp(const std::shared_ptr<ProjectPages>& pages,
                     const QString& outDir,
                     const QString& inputDir,
                     const ProjectReader* reader,
                     const DefaultParams& defaults,
                     const ProjectSession::LoadMode mode) {
  ProjectSession session;
  session.pages = pages;

  if (pages->numImages() == 0) {
    if (!inputDir.isEmpty()) {
      session.emptyProjectInputDir = inputDir;
    } else if (reader && !reader->directories().empty()) {
      session.emptyProjectInputDir = reader->directories().front();
    }
  }

  const bool withSideEffects = (mode == ProjectSession::LoadMode::Normal);
  if (withSideEffects && !outDir.isEmpty() && !QDir(outDir).exists()
      && project_files::isProjectInPlace(*pages, outDir, session.emptyProjectInputDir)) {
    // The output folder was deleted, e.g. to output everything again. The project is
    // where it was, so the folder is created again rather than asking for relinking.
    QDir().mkdir(outDir);
  }
  if (withSideEffects && !outDir.isEmpty()) {
    core::CoreUtils::maybeCreateCacheDir(outDir);
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

  session.stages = std::make_shared<StageSequence>(pages);
  // New projects start with the default parameters; older projects take what they lack from them.
  if (reader) {
    session.stages->loadProject(*reader, defaults);
  } else {
    session.stages->startNewProject(defaults);
  }

  // Thumbnails are stored relative to the output directory. Loading without side effects leaves
  // the cache on disk and this pointer null, so describing a project changes nothing.
  if (withSideEffects && !outDir.isEmpty()) {
    session.thumbnailCache = core::CoreUtils::createThumbnailCache(outDir);
  }
  return session;
}
}  // namespace

ProjectSession ProjectSession::createNew(const std::shared_ptr<ProjectPages>& pages,
                                         const QString& outDir,
                                         const QString& inputDir,
                                         const DefaultParams& startValues) {
  return setUp(pages, outDir, inputDir, nullptr, startValues, ProjectSession::LoadMode::Normal);
}

ProjectSession ProjectSession::load(const ProjectReader& reader,
                                    const DefaultParams& fallback,
                                    const ProjectSession::LoadMode mode) {
  return setUp(reader.pages(), reader.outputDirectory(), QString(), &reader, fallback, mode);
}

bool ProjectSession::write(const QString& projectFile) const {
  return ProjectWriter(pages, selectedPage, outFileNameGen, emptyProjectInputDir).write(projectFile, *stages);
}
