// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_AUTOIMPORTPANEL_H_
#define SCANTAILOR_APP_AUTOIMPORTPANEL_H_

#include <QString>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QRadioButton;

/**
 * \brief The "Automatic import" group above the options of the first step.
 *
 * Only shows and reports the state; MainWindow does the watching.
 */
class AutoImportPanel : public QWidget {
  Q_OBJECT
 public:
  explicit AutoImportPanel(QWidget* parent = nullptr);

  ~AutoImportPanel() override;

  /** Sets the check box without emitting importToggled(). */
  void setImporting(bool importing);

  bool isImporting() const;

  /** Importing needs a saved project; otherwise the check box is disabled. */
  void setImportPossible(bool possible);

  void setDirectory(const QString& dir);

  /** Shows a warning that \p count imported scans have no DPI. */
  void setScansWithoutDpi(int count);

 signals:

  void importToggled(bool importing);

  void changeDirectoryRequested();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void updateDirectoryLabel();

  void updateEnabled();

  QCheckBox* m_importCheck;
  QLabel* m_dirLabel;
  QPushButton* m_changeDirBtn;
  QRadioButton* m_appendMode;
  QRadioButton* m_insertAfterMode;
  QRadioButton* m_replaceMode;
  QLabel* m_noDpiLabel;
  QString m_dir;
};

#endif  // SCANTAILOR_APP_AUTOIMPORTPANEL_H_
