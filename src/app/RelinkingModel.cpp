// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "RelinkingModel.h"

#include <foundation/Hashes.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QWaitCondition>
#include <algorithm>
#include <array>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <cassert>

#include "OutOfMemoryHandler.h"
#include "PayloadEvent.h"
#include "StateIcon.h"

/**
 * A status request targets either a file (target >= 0, the index into m_items)
 * or a folder node (target < 0, see groupTarget()).
 */
class RelinkingModel::StatusUpdateResponse {
 public:
  StatusUpdateResponse(const QString& path, int target, Status status)
      : m_path(path), m_target(target), m_status(status) {}

  const QString& path() const { return m_path; }

  int target() const { return m_target; }

  Status status() const { return m_status; }

 private:
  QString m_path;
  int m_target;
  Status m_status;
};


class RelinkingModel::StatusUpdateThread : private QThread {
 public:
  explicit StatusUpdateThread(RelinkingModel* owner);

  /** This will signal the thread to stop and wait for it to happen. */
  ~StatusUpdateThread() override;

  /**
   * Requests are served from last to first.
   * Requesting the same item multiple times will just move the existing
   * record to the top of the stack.
   */
  void requestStatusUpdate(const QString& path, int target);

 private:
  struct Task {
    QString path;
    int target;
    QString key;

    /**< Different targets may share a path, so a task is identified by both. */

    Task(const QString& p, int t) : path(p), target(t), key(QString::number(t) + QChar('|') + p) {}
  };

  class HashedByKeyTag;
  class OrderedByPriorityTag;

  using TaskList = boost::multi_index_container<
      Task,
      boost::multi_index::indexed_by<
          boost::multi_index::hashed_unique<boost::multi_index::tag<HashedByKeyTag>,
                                            boost::multi_index::member<Task, QString, &Task::key>,
                                            hashes::hash<QString>>,
          boost::multi_index::sequenced<boost::multi_index::tag<OrderedByPriorityTag>>>>;

  using TasksByKey = TaskList::index<HashedByKeyTag>::type;
  using TasksByPriority = TaskList::index<OrderedByPriorityTag>::type;

  void run() override;

  RelinkingModel* m_owner;
  TaskList m_tasks;
  TasksByKey& m_tasksByKey;
  TasksByPriority& m_tasksByPriority;
  QString m_keyBeingProcessed;
  QMutex m_mutex;
  QWaitCondition m_cond;
  bool m_exiting;
};


namespace {
int groupTarget(const int group) {
  return -1 - group;
}
}  // namespace

/*============================ RelinkingModel =============================*/

RelinkingModel::RelinkingModel()
    : m_outputItem(-1),
      m_relinker(std::make_shared<Relinker>()),
      m_statusUpdateThread(std::make_unique<StatusUpdateThread>(this)),
      m_haveUncommittedChanges(true) {}

RelinkingModel::~RelinkingModel() = default;

QModelIndex RelinkingModel::index(const int row, const int column, const QModelIndex& parent) const {
  if (!hasIndex(row, column, parent)) {
    return QModelIndex();
  }
  if (!parent.isValid()) {
    return createIndex(row, column, quintptr(0));
  }
  // A file node keeps (its folder node + 1) as the internal id.
  return createIndex(row, column, quintptr(parent.row() + 1));
}

QModelIndex RelinkingModel::parent(const QModelIndex& child) const {
  if (!child.isValid() || (child.internalId() == 0)) {
    return QModelIndex();
  }
  return groupIndex(static_cast<int>(child.internalId()) - 1);
}

int RelinkingModel::rowCount(const QModelIndex& parent) const {
  if (!parent.isValid()) {
    return static_cast<int>(m_groups.size());
  }
  if ((parent.column() == 0) && (parent.internalId() == 0)) {
    return static_cast<int>(m_groups[parent.row()].items.size());
  }
  return 0;
}

int RelinkingModel::columnCount(const QModelIndex&) const {
  return 2;
}

QVariant RelinkingModel::data(const QModelIndex& index, const int role) const {
  if (!index.isValid()) {
    return QVariant();
  }

  if (index.internalId() == 0) {
    const Group& group = m_groups[index.row()];
    switch (role) {
      case TypeRole:
        return RelinkablePath::Dir;
      case UncommittedPathRole:
        return group.uncommittedDir;
      case HasMissingRole:
        return (group.uncommittedStatus == Missing) || (missingCountInGroup(group) > 0);
      case Qt::DisplayRole:
        if (index.column() == 0) {
          return toDisplayPath(group.uncommittedDir);
        }
        return QCoreApplication::translate("RelinkingModel", "%1 of %2 missing")
            .arg(missingCountInGroup(group))
            .arg(static_cast<int>(group.items.size()));
      case Qt::ToolTipRole:
        return toDisplayPath(group.uncommittedDir);
      case Qt::DecorationRole:
        if (index.column() == 0) {
          return stateIcon(groupState(group));
        }
        break;
      case Qt::TextAlignmentRole:
        if (index.column() == 1) {
          return int(Qt::AlignRight | Qt::AlignVCenter);
        }
        break;
      default:
        break;
    }
    return QVariant();
  }

  const Group& group = m_groups[index.internalId() - 1];
  const Item& item = m_items[group.items[index.row()]];
  switch (role) {
    case TypeRole:
      return item.type;
    case UncommittedPathRole:
      return item.uncommittedPath;
    case HasMissingRole:
      return item.uncommittedStatus == Missing;
    case Qt::DisplayRole:
      if (index.column() == 0) {
        // A file relinked on its own may have left the folder of its node.
        if (parentDir(item.uncommittedPath) == group.uncommittedDir) {
          return item.uncommittedPath.mid(group.uncommittedDir.length() + 1);
        }
        return toDisplayPath(item.uncommittedPath);
      }
      break;
    case Qt::ToolTipRole:
      return toDisplayPath(item.uncommittedPath);
    case Qt::DecorationRole:
      if (index.column() == 0) {
        switch (item.uncommittedStatus) {
          case Exists:
            return stateIcon(AllPresent);
          case Missing:
            return stateIcon(FolderMissing);
          case StatusUpdatePending:
            return stateIcon(StatePending);
        }
      }
      break;
    default:
      break;
  }
  return QVariant();
}

void RelinkingModel::addPath(const RelinkablePath& path) {
  const std::pair<std::set<QString>::iterator, bool> ins(m_origPathSet.insert(path.normalizedPath()));
  if (!ins.second) {
    // Not inserted because identical path is already there.
    return;
  }

  const int itemIdx = static_cast<int>(m_items.size());

  if (path.type() == RelinkablePath::Dir) {
    if (m_outputItem >= 0) {
      return;  // Only one output folder is expected.
    }
    m_items.emplace_back(path);
    m_outputItem = itemIdx;
    requestItemStatusUpdate(itemIdx);
    return;
  }

  const QString dir(parentDir(path.normalizedPath()));
  auto it = m_groupByOrigDir.find(dir);
  if (it == m_groupByOrigDir.end()) {
    const int groupIdx = static_cast<int>(m_groups.size());
    beginInsertRows(QModelIndex(), groupIdx, groupIdx);
    m_groups.emplace_back(dir);
    m_groupByOrigDir.emplace(dir, groupIdx);
    endInsertRows();
    requestGroupStatusUpdate(groupIdx);
    it = m_groupByOrigDir.find(dir);
  }

  const int groupIdx = it->second;
  Group& group = m_groups[groupIdx];
  const int pos = static_cast<int>(group.items.size());

  beginInsertRows(groupIndex(groupIdx), pos, pos);
  m_items.emplace_back(path);
  m_items.back().group = groupIdx;
  m_items.back().posInGroup = pos;
  group.items.push_back(itemIdx);
  endInsertRows();

  requestItemStatusUpdate(itemIdx);
}

bool RelinkingModel::replacePrefix(const QString prefix,
                                   const QString replacement,
                                   const RelinkablePath::Type type,
                                   Merge* merge) {
  replacePrefixUncommitted(prefix, replacement, type);
  if (findMerge(merge)) {
    rollbackChanges();
    return false;
  }
  commitChanges();
  return true;
}

void RelinkingModel::replacePrefixUncommitted(const QString& prefix,
                                              const QString& replacement,
                                              const RelinkablePath::Type type) {
  std::set<int> changedGroups;

  for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
    Item& item = m_items[i];
    bool modified = false;

    if (type == RelinkablePath::File) {
      if ((item.type == RelinkablePath::File) && (item.uncommittedPath == prefix)) {
        item.uncommittedPath = replacement;
        modified = true;
      }
    } else {
      assert(type == RelinkablePath::Dir);
      modified = replacePathPrefix(item.uncommittedPath, prefix, replacement);
    }

    if (modified) {
      m_haveUncommittedChanges = true;
      requestItemStatusUpdate(i);  // This sets item.uncommittedStatus to StatusUpdatePending.
      emitItemChanged(i);
      if (item.group >= 0) {
        changedGroups.insert(item.group);
      }
    }
  }

  if (type == RelinkablePath::Dir) {
    for (int g = 0; g < static_cast<int>(m_groups.size()); ++g) {
      if (replacePathPrefix(m_groups[g].uncommittedDir, prefix, replacement)) {
        m_haveUncommittedChanges = true;
        requestGroupStatusUpdate(g);
        changedGroups.insert(g);
        // The display of files depends on the folder path.
        for (const int i : m_groups[g].items) {
          emitItemChanged(i);
        }
      }
    }
  }

  for (const int g : changedGroups) {
    emitGroupChanged(g);
  }
}  // RelinkingModel::replacePrefixUncommitted

bool RelinkingModel::findMerge(Merge* merge) const {
  // Pairs of (new path, original path), sorted by the new path.
  std::vector<std::pair<QString, QString>> paths;
  paths.reserve(m_items.size());
  for (const Item& item : m_items) {
    paths.emplace_back(item.uncommittedPath, item.origPath);
  }
  std::sort(paths.begin(), paths.end());

  Merge found;
  for (size_t i = 1; i < paths.size(); ++i) {
    if ((paths[i].first != paths[i - 1].first) || ((i >= 2) && (paths[i].first == paths[i - 2].first))) {
      continue;  // Each new path shared by several files counts once.
    }
    if (found.count == 0) {
      found.origPath1 = paths[i - 1].second;
      found.origPath2 = paths[i].second;
      found.newPath = paths[i].first;
    }
    ++found.count;
  }

  if (merge) {
    *merge = found;
  }
  return found.count > 0;
}

void RelinkingModel::commitChanges() {
  if (!m_haveUncommittedChanges) {
    return;
  }

  Relinker newRelinker;

  for (Item& item : m_items) {
    // Changes committed earlier have to stay in the relinker, too.
    if (item.origPath != item.uncommittedPath) {
      newRelinker.addMapping(item.origPath, item.uncommittedPath);
    }

    item.committedPath = item.uncommittedPath;
    item.committedStatus = item.uncommittedStatus;
  }

  for (Group& group : m_groups) {
    group.committedDir = group.uncommittedDir;
    group.committedStatus = group.uncommittedStatus;
  }

  m_relinker->swap(newRelinker);
  m_haveUncommittedChanges = false;
}  // RelinkingModel::commitChanges

void RelinkingModel::rollbackChanges() {
  if (!m_haveUncommittedChanges) {
    return;
  }

  std::set<int> changedGroups;

  for (int i = 0; i < static_cast<int>(m_items.size()); ++i) {
    Item& item = m_items[i];
    if (item.uncommittedPath != item.committedPath) {
      item.uncommittedPath = item.committedPath;
      item.uncommittedStatus = item.committedStatus;
      emitItemChanged(i);
      if (item.group >= 0) {
        changedGroups.insert(item.group);
      }
    }
  }

  for (int g = 0; g < static_cast<int>(m_groups.size()); ++g) {
    Group& group = m_groups[g];
    if (group.uncommittedDir != group.committedDir) {
      group.uncommittedDir = group.committedDir;
      group.uncommittedStatus = group.committedStatus;
      changedGroups.insert(g);
      for (const int i : group.items) {
        emitItemChanged(i);
      }
    }
  }

  for (const int g : changedGroups) {
    emitGroupChanged(g);
  }

  m_haveUncommittedChanges = false;
}  // RelinkingModel::rollbackChanges

QString RelinkingModel::outputDir() const {
  return (m_outputItem >= 0) ? m_items[m_outputItem].uncommittedPath : QString();
}

void RelinkingModel::setOutputDir(const QString& path) {
  if (m_outputItem < 0) {
    return;
  }

  Item& item = m_items[m_outputItem];
  item.committedPath = path;
  item.uncommittedPath = path;
  requestItemStatusUpdate(m_outputItem);
  item.committedStatus = item.uncommittedStatus;
  m_relinker->addMapping(item.origPath, path);
}

int RelinkingModel::fileCount() const {
  return static_cast<int>(m_items.size()) - (m_outputItem >= 0 ? 1 : 0);
}

int RelinkingModel::missingFileCount() const {
  int count = 0;
  for (const Group& group : m_groups) {
    count += missingCountInGroup(group);
  }
  return count;
}

bool RelinkingModel::isStatusUpdatePending() const {
  for (const Group& group : m_groups) {
    if (group.uncommittedStatus == StatusUpdatePending) {
      return true;
    }
  }
  for (const Item& item : m_items) {
    if ((item.group >= 0) && (item.uncommittedStatus == StatusUpdatePending)) {
      return true;
    }
  }
  return false;
}

bool RelinkingModel::isPartiallyMissingFolder(const QModelIndex& index) const {
  if (!index.isValid() || (index.internalId() != 0)) {
    return false;
  }
  return groupState(m_groups[index.row()]) == SomeMissing;
}

QPixmap RelinkingModel::stateIcon(const State state) {
  static const std::array<StateIcon::Color, 4> colors{StateIcon::Green, StateIcon::Yellow, StateIcon::Red,
                                                      StateIcon::None};
  return StateIcon::pixmap(colors[state]);
}

QString RelinkingModel::toDisplayPath(const QString& path) {
  if (path.startsWith(QChar('/')) && !path.startsWith(QLatin1String("//"))) {
    // "//" indicates a network path
    return path;
  }
  return QDir::toNativeSeparators(path);
}

void RelinkingModel::ensureEndsWithSlash(QString& str) {
  if (!str.endsWith(QChar('/'))) {
    str += QChar('/');
  }
}

bool RelinkingModel::replacePathPrefix(QString& path, const QString& prefix, const QString& replacement) {
  QString slashTerminatedPrefix(prefix);
  ensureEndsWithSlash(slashTerminatedPrefix);

  if (path.startsWith(slashTerminatedPrefix)) {
    const int suffixLen = path.length() - slashTerminatedPrefix.length() + 1;
    path = replacement + path.right(suffixLen);
    return true;
  }
  if (path == prefix) {
    path = replacement;
    return true;
  }
  return false;
}

QString RelinkingModel::parentDir(const QString& path) {
  return path.left(path.lastIndexOf(QChar('/')));
}

QModelIndex RelinkingModel::groupIndex(const int group, const int column) const {
  return createIndex(group, column, quintptr(0));
}

QModelIndex RelinkingModel::itemIndex(const int item, const int column) const {
  const Item& it = m_items[item];
  if (it.group < 0) {
    return QModelIndex();
  }
  return createIndex(it.posInGroup, column, quintptr(it.group + 1));
}

void RelinkingModel::emitGroupChanged(const int group) {
  emit dataChanged(groupIndex(group, 0), groupIndex(group, 1));
}

void RelinkingModel::emitItemChanged(const int item) {
  if (m_items[item].group >= 0) {
    emit dataChanged(itemIndex(item, 0), itemIndex(item, 1));
  }
}

int RelinkingModel::missingCountInGroup(const Group& group) const {
  int count = 0;
  for (const int i : group.items) {
    if (m_items[i].uncommittedStatus == Missing) {
      ++count;
    }
  }
  return count;
}

RelinkingModel::State RelinkingModel::groupState(const Group& group) const {
  if (group.uncommittedStatus == Missing) {
    return FolderMissing;
  }
  if (group.uncommittedStatus == StatusUpdatePending) {
    return StatePending;
  }
  bool pending = false;
  for (const int i : group.items) {
    if (m_items[i].uncommittedStatus == Missing) {
      return SomeMissing;
    }
    if (m_items[i].uncommittedStatus == StatusUpdatePending) {
      pending = true;
    }
  }
  return pending ? StatePending : AllPresent;
}

void RelinkingModel::requestItemStatusUpdate(const int item) {
  Item& it = m_items[item];
  it.uncommittedStatus = StatusUpdatePending;
  m_statusUpdateThread->requestStatusUpdate(it.uncommittedPath, item);
}

void RelinkingModel::requestGroupStatusUpdate(const int group) {
  Group& gr = m_groups[group];
  gr.uncommittedStatus = StatusUpdatePending;
  m_statusUpdateThread->requestStatusUpdate(gr.uncommittedDir, groupTarget(group));
}

void RelinkingModel::customEvent(QEvent* event) {
  using ResponseEvent = PayloadEvent<StatusUpdateResponse>;
  auto* evt = dynamic_cast<ResponseEvent*>(event);
  assert(evt);

  const StatusUpdateResponse& response = evt->payload();
  const int target = response.target();

  if (target < 0) {
    const int g = groupTarget(target);
    if (g >= int(m_groups.size())) {
      return;
    }
    Group& group = m_groups[g];
    if (group.uncommittedDir == response.path()) {
      group.uncommittedStatus = response.status();
    }
    if (group.committedDir == response.path()) {
      group.committedStatus = response.status();
    }
    emitGroupChanged(g);
    return;
  }

  if (target >= int(m_items.size())) {
    return;
  }

  Item& item = m_items[target];
  if (item.uncommittedPath == response.path()) {
    item.uncommittedStatus = response.status();
  }
  if (item.committedPath == response.path()) {
    item.committedStatus = response.status();
  }

  emitItemChanged(target);
  if (item.group >= 0) {
    emitGroupChanged(item.group);
  }
}

/*========================== StatusUpdateThread =========================*/

RelinkingModel::StatusUpdateThread::StatusUpdateThread(RelinkingModel* owner)
    : QThread(owner),
      m_owner(owner),
      m_tasks(),
      m_tasksByKey(m_tasks.get<HashedByKeyTag>()),
      m_tasksByPriority(m_tasks.get<OrderedByPriorityTag>()),
      m_exiting(false) {}

RelinkingModel::StatusUpdateThread::~StatusUpdateThread() {
  {
    QMutexLocker locker(&m_mutex);
    m_exiting = true;
  }

  m_cond.wakeAll();
  wait();
}

void RelinkingModel::StatusUpdateThread::requestStatusUpdate(const QString& path, int target) {
  const QMutexLocker locker(&m_mutex);
  if (m_exiting) {
    return;
  }

  const Task newTask(path, target);
  if (newTask.key == m_keyBeingProcessed) {
    // This task is currently in progress.
    return;
  }

  const std::pair<TasksByKey::iterator, bool> ins(m_tasksByKey.insert(newTask));

  // Whether inserted or being already there, move it to the front of priority queue.
  m_tasksByPriority.relocate(m_tasksByPriority.end(), m_tasks.project<OrderedByPriorityTag>(ins.first));

  if (!isRunning()) {
    start();
  }

  m_cond.wakeOne();
}

void RelinkingModel::StatusUpdateThread::run() try {
  const QMutexLocker locker(&m_mutex);

  class MutexUnlocker {
   public:
    explicit MutexUnlocker(QMutex* mutex) : m_mutex(mutex) { mutex->unlock(); }

    ~MutexUnlocker() { m_mutex->lock(); }

   private:
    QMutex* const m_mutex;
  };


  while (true) {
    if (m_exiting) {
      break;
    }

    if (m_tasks.empty()) {
      m_cond.wait(&m_mutex);
    }

    if (m_tasks.empty()) {
      continue;
    }

    const Task task(m_tasksByPriority.front());
    m_keyBeingProcessed = task.key;
    m_tasksByPriority.pop_front();

    {
      const MutexUnlocker unlocker(&m_mutex);

      const bool exists = QFile::exists(task.path);
      const StatusUpdateResponse response(task.path, task.target, exists ? Exists : Missing);
      QCoreApplication::postEvent(m_owner, new PayloadEvent<StatusUpdateResponse>(response));
    }

    m_keyBeingProcessed.clear();
  }
}  // RelinkingModel::StatusUpdateThread::run

catch (const std::bad_alloc&) {
  OutOfMemoryHandler::instance().handleOutOfMemorySituation();
}

/*================================ Item =================================*/

RelinkingModel::Item::Item(const RelinkablePath& path)
    : origPath(path.normalizedPath()),
      committedPath(path.normalizedPath()),
      uncommittedPath(path.normalizedPath()),
      type(path.type()),
      committedStatus(StatusUpdatePending),
      uncommittedStatus(StatusUpdatePending),
      group(-1),
      posInGroup(-1) {}

/*================================ Group ================================*/

RelinkingModel::Group::Group(const QString& dir)
    : origDir(dir),
      committedDir(dir),
      uncommittedDir(dir),
      committedStatus(StatusUpdatePending),
      uncommittedStatus(StatusUpdatePending) {}

/*============================== Relinker ================================*/

void RelinkingModel::Relinker::addMapping(const QString& from, const QString& to) {
  m_mappings[from] = to;
}

QString RelinkingModel::Relinker::substitutionPathFor(const RelinkablePath& path) const {
  const auto it(m_mappings.find(path.normalizedPath()));
  if (it != m_mappings.end()) {
    return it->second;
  } else {
    return path.normalizedPath();
  }
}

void RelinkingModel::Relinker::swap(Relinker& other) {
  m_mappings.swap(other.m_mappings);
}
