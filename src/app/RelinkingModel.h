// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_APP_RELINKINGMODEL_H_
#define SCANTAILOR_APP_RELINKINGMODEL_H_

#include <QAbstractItemModel>
#include <QModelIndex>
#include <QPixmap>
#include <QString>
#include <QThread>
#include <Qt>
#include <memory>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "AbstractRelinker.h"
#include "Hashes.h"
#include "NonCopyable.h"
#include "RelinkablePath.h"
#include "VirtualFunction.h"

/**
 * Image files are presented as a two-level tree: folders (by their original location)
 * with their files below. The output folder is kept apart from the tree, see outputDir().
 */
class RelinkingModel : public QAbstractItemModel {
  DECLARE_NON_COPYABLE(RelinkingModel)

 public:
  enum Status { Exists, Missing, StatusUpdatePending };

  /** The state of a folder node, see stateIcon(). */
  enum State { AllPresent, SomeMissing, FolderMissing, StatePending };

  enum { TypeRole = Qt::UserRole, PathRole, HasMissingRole };

  RelinkingModel();

  ~RelinkingModel() override;

  QModelIndex index(int row, int column, const QModelIndex& parent = QModelIndex()) const override;

  QModelIndex parent(const QModelIndex& child) const override;

  int rowCount(const QModelIndex& parent = QModelIndex()) const override;

  int columnCount(const QModelIndex& parent = QModelIndex()) const override;

  QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

  /**
   * Always returns the same relinker, which follows all changes made later.
   * This allows you to take the relinker right after construction
   * and then use it when accepted() signal is emitted.
   */
  std::shared_ptr<AbstractRelinker> relinker() const { return m_relinker; }

  void operator()(const RelinkablePath& path) { addPath(path); }

  /** Files go into the tree, the first folder becomes the output folder. */
  void addPath(const RelinkablePath& path);

  /** Describes a change that would remap different original paths to the same one. */
  struct Merge {
    QString origPath1;
    QString origPath2;
    QString newPath;
    int count = 0;
  };

  /**
   * Replaces \p prefix with \p replacement in all paths, taking effect at once.
   * A change that would merge different files into one is not applied:
   * false is returned and \p merge describes it.
   * The paths are taken by value, as views may change the caller's strings while the model emits its signals.
   */
  bool replacePrefix(QString prefix, QString replacement, RelinkablePath::Type type, Merge* merge);

  bool hasOutputDir() const { return m_outputItem >= 0; }

  QString outputDir() const;

  /** Relinks the output folder alone. */
  void setOutputDir(const QString& path);

  int fileCount() const;

  int missingFileCount() const;

  /** Returns true while the status of some file or folder is still unknown. */
  bool isStatusUpdatePending() const;

  /** Returns true if \p index is a folder node and its folder exists but some of its files don't. */
  bool isPartiallyMissingFolder(const QModelIndex& index) const;

  static QPixmap stateIcon(State state);

  static QString toDisplayPath(const QString& path);

 protected:
  void customEvent(QEvent* event) override;

 private:
  class StatusUpdateThread;
  class StatusUpdateResponse;

  /** Stands for File System Object (file or directory). */
  struct Item {
    /** The path passed through addPath(). It never changes. */
    QString origPath;
    /** The path after all relinkings so far. */
    QString path;
    RelinkablePath::Type type;
    Status status;
    /** The folder node the file belongs to, or -1 for the output folder. */
    int group;
    int posInGroup;

    explicit Item(const RelinkablePath& relinkablePath);
  };

  /** A folder node. Its path follows folder relinkings the same way the paths of its files do. */
  struct Group {
    QString dir;
    Status status;
    std::vector<int> items;

    explicit Group(const QString& folder);
  };

  class Relinker : public AbstractRelinker {
   public:
    void addMapping(const QString& from, const QString& to);

    /**
     * Returns path.normalizedPath() if there is no mapping for it, unless an image is relinked
     * to that path, see the implementation.
     */
    QString substitutionPathFor(const RelinkablePath& path) const override;

    void swap(Relinker& other);

   private:
    std::unordered_map<QString, QString, hashes::hash<QString>> m_mappings;
    /** The paths mapped to. */
    std::unordered_set<QString, hashes::hash<QString>> m_targets;
  };


  static void ensureEndsWithSlash(QString& str);

  static bool replacePathPrefix(QString& path, const QString& prefix, const QString& replacement);

  static QString parentDir(const QString& path);

  /** Returns true if \p newPaths (one per item) would remap different original paths to the same one. */
  bool findMerge(const std::vector<QString>& newPaths, Merge* merge) const;

  /** Gives the relinker the mappings of all items that have been relinked. */
  void updateRelinker();

  QModelIndex groupIndex(int group, int column = 0) const;

  QModelIndex itemIndex(int item, int column = 0) const;

  void emitGroupChanged(int group);

  void emitItemChanged(int item);

  int missingCountInGroup(const Group& group) const;

  State groupState(const Group& group) const;

  void requestItemStatusUpdate(int item);

  void requestGroupStatusUpdate(int group);

  std::vector<Item> m_items;
  std::vector<Group> m_groups;
  std::unordered_map<QString, int, hashes::hash<QString>> m_groupByOrigDir;
  std::set<QString> m_origPathSet;
  int m_outputItem;
  const std::shared_ptr<Relinker> m_relinker;
  std::unique_ptr<StatusUpdateThread> m_statusUpdateThread;
};


#endif  // ifndef SCANTAILOR_APP_RELINKINGMODEL_H_
