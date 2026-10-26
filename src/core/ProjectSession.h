// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PROJECTSESSION_H_
#define SCANTAILOR_CORE_PROJECTSESSION_H_

#include <QString>
#include <memory>

#include "OutputFileNameGenerator.h"
#include "SelectedPage.h"

class DefaultParams;
class PageSelectionAccessor;
class ProjectPages;
class ProjectReader;
class StageSequence;
class ThumbnailPixmapCache;

/**
 * An open project as processing needs it: its pages, its steps with their settings and where
 * the output goes. The app takes the parts over into its window; a command line tool or a
 * test uses them directly.
 */
struct ProjectSession {
  /**
   * A new project of \p pages. \p inputDir is the folder its images come from; a project
   * without images remembers it. \p outDir may be empty for no project at all.
   */
  static ProjectSession createNew(const std::shared_ptr<ProjectPages>& pages,
                                  const QString& outDir,
                                  const QString& inputDir,
                                  const DefaultParams& startValues,
                                  const PageSelectionAccessor& pageSelectionAccessor);

  /** A project read from its file. Settings the file lacks come from \p fallback. */
  static ProjectSession load(const ProjectReader& reader,
                             const DefaultParams& fallback,
                             const PageSelectionAccessor& pageSelectionAccessor);

  /** Writes the project file. Returns false if it could not be written. */
  bool write(const QString& projectFile) const;

  std::shared_ptr<ProjectPages> pages;
  std::shared_ptr<StageSequence> stages;
  OutputFileNameGenerator outFileNameGen;
  /** Null without an output folder. */
  std::shared_ptr<ThumbnailPixmapCache> thumbnailCache;
  SelectedPage selectedPage;
  /** Only for a project without images: the folder they are to come from. */
  QString emptyProjectInputDir;
};

#endif  // SCANTAILOR_CORE_PROJECTSESSION_H_
