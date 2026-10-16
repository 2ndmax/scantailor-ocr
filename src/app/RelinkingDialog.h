// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_RELINKINGDIALOG_H_
#define SCANTAILOR_APP_RELINKINGDIALOG_H_

#include <QDialog>
#include <QTimer>
#include <memory>

#include "AbstractRelinker.h"
#include "RelinkablePath.h"
#include "RelinkingModel.h"
#include "ui_RelinkingDialog.h"

class RelinkingSortingModel;
class QAbstractButton;
class QString;

class RelinkingDialog : public QDialog {
  Q_OBJECT
 public:
  explicit RelinkingDialog(const QString& projectFilePath, QWidget* parent = nullptr);

  ProxyFunction<RelinkingModel&, void, const RelinkablePath&> pathCollector() {
    return ProxyFunction<RelinkingModel&, void, const RelinkablePath&>(m_model);
  }

  /**
   * Always returns the same relinker, which follows all changes made later.
   * This allows you to take the relinker right after construction
   * and then use it when accepted() signal is emitted.
   */
  std::shared_ptr<AbstractRelinker> relinker() const { return m_model.relinker(); }

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;

 private slots:

  void selectionChanged();

  void nodePathEdited();

  void locateClicked();

  void okClicked();

  void outputDirEdited();

  void outputDirBrowseClicked();

  void showMissingOnlyToggled(bool checked);

  void updateStatus();

 private:
  /** Relinks the selected folder or file as a whole to \p path. */
  void applyNodePath(const QString& path);

  void refreshNodePath();

  void refreshNodeStatus();

  void refreshOutputDir();

  void applyInitialState();

  Ui::RelinkingDialog ui;
  RelinkingModel m_model;
  RelinkingSortingModel* m_sortingModel;
  QString m_projectFileDir;
  /** The current path of the selected folder or file, empty if nothing is selected. */
  QString m_nodePath;
  int m_nodeType;
  QTimer m_statusUpdateTimer;
  bool m_outputDirShown;
  bool m_initialStateApplied;
  bool m_showMissingOnlyTouched;
};


#endif  // ifndef SCANTAILOR_APP_RELINKINGDIALOG_H_
