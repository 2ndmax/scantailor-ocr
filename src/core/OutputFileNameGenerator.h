// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OUTPUTFILENAMEGENERATOR_H_
#define SCANTAILOR_CORE_OUTPUTFILENAMEGENERATOR_H_

#include <QString>
#include <QStringList>
#include <Qt>
#include <memory>
#include <vector>

#include "FileNameDisambiguator.h"

class ImageId;
class PageId;
class AbstractRelinker;

class OutputFileNameGenerator {
  // Member-wise copying is OK.
 public:
  OutputFileNameGenerator();

  OutputFileNameGenerator(std::shared_ptr<FileNameDisambiguator> disambiguator,
                          const QString& outDir,
                          Qt::LayoutDirection layoutDirection);

  void performRelinking(const AbstractRelinker& relinker);

  void setLayoutDirection(Qt::LayoutDirection dir) { m_layoutDirection = dir; }

  /**
   * \brief Sets the layout direction and renames the existing output files of the halves of \p images to match.
   *
   * The direction decides the number in the name of each half: _1L and _2R become _2L and _1R, or the other
   * way round. The files keep their size and modification time, so they aren't generated again. A file
   * already present under a new name is replaced.
   *
   * \param dirs The directories holding files named after the page.
   */
  void setLayoutDirectionRenamingFiles(Qt::LayoutDirection dir,
                                       const std::vector<ImageId>& images,
                                       const QStringList& dirs);

  Qt::LayoutDirection layoutDirection() const { return m_layoutDirection; }

  const QString& outDir() const { return m_outDir; }

  FileNameDisambiguator* disambiguator() { return m_disambiguator.get(); }

  const FileNameDisambiguator* disambiguator() const { return m_disambiguator.get(); }

  QString fileNameFor(const PageId& page) const;

  QString filePathFor(const PageId& page) const;

  /**
   * \brief Removes the output files of \p page.
   *
   * \param dirs The directories holding files named after the page.
   */
  void removeFiles(const PageId& page, const QStringList& dirs) const;

 private:
  std::shared_ptr<FileNameDisambiguator> m_disambiguator;
  QString m_outDir;
  Qt::LayoutDirection m_layoutDirection;
};


#endif  // ifndef SCANTAILOR_CORE_OUTPUTFILENAMEGENERATOR_H_
