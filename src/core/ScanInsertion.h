// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_SCANINSERTION_H_
#define SCANTAILOR_CORE_SCANINSERTION_H_

#include <QString>
#include <functional>

namespace scan_insertion {
/**
 * The name a new scan gets when it is inserted after \p anchorFile or replaces it:
 * "<name of anchorFile without extension>_<file name of scanFile>".  So "A.tif" and
 * "B.tif" give "A_B.tif", which is sorted right after "A.tif", and further scans
 * inserted after "A.tif" are sorted by their own (time stamp) names.
 */
QString prefixedFileName(const QString& anchorFile, const QString& scanFile);

/** The folder replaced images are moved to: "replaced" in the folder of the image. */
QString replacedDir(const QString& replacedFile);

/**
 * Where the replaced image \p replacedFile is moved to: into replacedDir(), under its name,
 * or with " (2)", " (3)" ... before the extension if \p exists says that name is taken.
 */
QString replacedFilePath(const QString& replacedFile, const std::function<bool(const QString&)>& exists);
}  // namespace scan_insertion

/**
 * \brief Where the next scan goes in the mode "Insert after selected page".
 *
 * The page selected when the mode is chosen is the anchor.  Each scan is inserted after
 * the one inserted before it and named after the anchor.  Selecting a page other than
 * the anchor and the last inserted one makes it the new anchor; the program itself
 * selects the last inserted page, and re-selects the current one e.g. when switching
 * steps, so these don't count.  Pages are identified by the paths of their image files.
 */
class ScanInsertionAnchor {
 public:
  /** Makes \p anchorFile the anchor; the next scan goes right after it. */
  void start(const QString& anchorFile);

  void clear();

  bool isActive() const { return !m_anchor.isEmpty(); }

  /** The file the names of new scans start with. */
  const QString& anchorFile() const { return m_anchor; }

  /** The file the next scan is inserted after: the last inserted one, or the anchor. */
  const QString& insertAfterFile() const { return m_last; }

  void inserted(const QString& file);

  /** A page was selected.  Returns true if it became the new anchor. */
  bool pageSelected(const QString& file);

 private:
  QString m_anchor;
  QString m_last;
};

#endif  // SCANTAILOR_CORE_SCANINSERTION_H_
