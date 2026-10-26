// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_PROJECTOPENINGCONTEXT_H_
#define SCANTAILOR_APP_PROJECTOPENINGCONTEXT_H_

#include <QObject>
#include <QPointer>
#include <QString>
#include <Qt>
#include <memory>
#include <vector>

#include "ImageFileInfo.h"
#include "NonCopyable.h"
#include "ProjectReader.h"

class FixDpiDialog;
class QWidget;

class ProjectOpeningContext : public QObject {
  Q_OBJECT
  DECLARE_NON_COPYABLE(ProjectOpeningContext)

 public:
  ProjectOpeningContext(QWidget* parent, const QString& projectFile);

  ~ProjectOpeningContext() override;

  void proceed();

  const QString& projectFile() const { return m_projectFile; }

  /** Null until proceed() has read the project file. */
  ProjectReader* projectReader() { return m_reader.get(); }

 signals:

  void done(ProjectOpeningContext* context);

 private slots:

  void fixedDpiSubmitted();

  void fixDpiDialogDestroyed();

 private:
  /**
   * For a project saved by ScanTailor Advanced: tells that saving it makes it unusable there and
   * offers to make a backup copy first. Returns false if the project is not to be opened.
   */
  bool confirmOlderVersion();

  void showFixDpiDialog();

  QString m_projectFile;
  std::unique_ptr<ProjectReader> m_reader;
  QPointer<FixDpiDialog> m_fixDpiDialog;
  QWidget* m_parent;
};


#endif  // ifndef SCANTAILOR_APP_PROJECTOPENINGCONTEXT_H_
