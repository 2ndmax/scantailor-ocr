// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "FileNameDisambiguator.h"

#include <QDomDocument>
#include <QFileInfo>
#include <QMutex>
#include <QRegularExpression>
#include <algorithm>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <vector>

#include "AbstractRelinker.h"
#include "RelinkablePath.h"

using namespace boost::multi_index;

namespace {
QString labelText(const int label) {
  return (label == 0) ? QString() : QString::fromLatin1("(%1)").arg(label);
}

/**
 * True if an output name of one stem can be an output name of the other one, as in
 * "x" + "_page0001" and "x_page0001", or "y" + "_1L" and "y_1L". The suffixes are those
 * OutputFileNameGenerator::fileNameFor() appends.
 */
bool stemsOverlap(const QString& stem1, const QString& stem2) {
  if (stem1 == stem2) {
    return true;
  }
  const QString& shorter = (stem1.size() < stem2.size()) ? stem1 : stem2;
  const QString& longer = (stem1.size() < stem2.size()) ? stem2 : stem1;
  if (!longer.startsWith(shorter)) {
    return false;
  }
  static const QRegularExpression suffixes("^(_page\\d{4,})?(_[12][lr])?$", QRegularExpression::CaseInsensitiveOption);
  return suffixes.match(longer.mid(shorter.size())).hasMatch();
}
}  // namespace

class FileNameDisambiguator::Impl {
 public:
  Impl();

  Impl(const QDomElement& disambiguatorEl, const std::function<QString(const QString&)>& filePathUnpacker);

  QDomElement toXml(QDomDocument& doc,
                    const QString& name,
                    const std::function<QString(const QString&)>& filePathPacker) const;

  int getLabel(const QString& filePath) const;

  int registerFile(const QString& filePath);

  void performRelinking(const AbstractRelinker& relinker);

 private:
  class ItemsByFilePathTag;
  class ItemsByFileNameLabelTag;

  class UnorderedItemsTag;

  struct Item {
    QString filePath;
    QString fileName;
    // The base name without extension, case folded; computed once for the comparisons.
    QString foldedBaseName;
    int label;

    Item(const QString& filePath, int lbl);
  };

  using Container = multi_index_container<
      Item,
      indexed_by<
          ordered_unique<tag<ItemsByFilePathTag>, member<Item, QString, &Item::filePath>>,
          ordered_unique<tag<ItemsByFileNameLabelTag>,
                         composite_key<Item, member<Item, QString, &Item::fileName>, member<Item, int, &Item::label>>>,
          sequenced<tag<UnorderedItemsTag>>>>;

  using ItemsByFilePath = Container::index<ItemsByFilePathTag>::type;
  using ItemsByFileNameLabel = Container::index<ItemsByFileNameLabelTag>::type;
  using UnorderedItems = Container::index<UnorderedItemsTag>::type;

  /** True if another item in items can have an output name that filePath has with this label. */
  static bool hasOverlappingItem(const Container& items, const QString& filePath, int label);

  /** The lowest label that gives filePath output names no other item in items can have. */
  static int freeLabel(const Container& items, const QString& filePath);

  mutable QMutex m_mutex;
  Container m_items;
  ItemsByFilePath& m_itemsByFilePath;
  ItemsByFileNameLabel& m_itemsByFileNameLabel;
  UnorderedItems& m_unorderedItems;
};


/*====================== FileNameDisambiguator =========================*/

FileNameDisambiguator::FileNameDisambiguator() : m_impl(std::make_unique<Impl>()) {}

FileNameDisambiguator::FileNameDisambiguator(const QDomElement& disambiguatorEl)
    : m_impl(std::make_unique<Impl>(disambiguatorEl, [](const QString& path) { return path; })) {}

FileNameDisambiguator::FileNameDisambiguator(const QDomElement& disambiguatorEl,
                                             const std::function<QString(const QString&)>& filePathUnpacker)
    : m_impl(std::make_unique<Impl>(disambiguatorEl, filePathUnpacker)) {}

FileNameDisambiguator::~FileNameDisambiguator() = default;

QDomElement FileNameDisambiguator::toXml(QDomDocument& doc, const QString& name) const {
  return m_impl->toXml(doc, name, [](const QString& path) { return path; });
}

QDomElement FileNameDisambiguator::toXml(QDomDocument& doc,
                                         const QString& name,
                                         const std::function<QString(const QString&)>& filePathPacker) const {
  return m_impl->toXml(doc, name, filePathPacker);
}

int FileNameDisambiguator::getLabel(const QString& filePath) const {
  return m_impl->getLabel(filePath);
}

int FileNameDisambiguator::registerFile(const QString& filePath) {
  return m_impl->registerFile(filePath);
}

void FileNameDisambiguator::performRelinking(const AbstractRelinker& relinker) {
  m_impl->performRelinking(relinker);
}

QString FileNameDisambiguator::outputStem(const QString& filePath, const int label) {
  return QFileInfo(filePath).completeBaseName() + labelText(label);
}

/*==================== FileNameDisambiguator::Impl ====================*/

FileNameDisambiguator::Impl::Impl()
    : m_items(),
      m_itemsByFilePath(m_items.get<ItemsByFilePathTag>()),
      m_itemsByFileNameLabel(m_items.get<ItemsByFileNameLabelTag>()),
      m_unorderedItems(m_items.get<UnorderedItemsTag>()) {}

FileNameDisambiguator::Impl::Impl(const QDomElement& disambiguatorEl,
                                  const std::function<QString(const QString&)>& filePathUnpacker)
    : m_items(),
      m_itemsByFilePath(m_items.get<ItemsByFilePathTag>()),
      m_itemsByFileNameLabel(m_items.get<ItemsByFileNameLabelTag>()),
      m_unorderedItems(m_items.get<UnorderedItemsTag>()) {
  QDomNode node(disambiguatorEl.firstChild());
  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != "mapping") {
      continue;
    }
    const QDomElement fileEl(node.toElement());

    const QString filePathShorthand(fileEl.attribute("file"));
    const QString filePath = filePathUnpacker(filePathShorthand);
    if (filePath.isEmpty()) {
      // Unresolved shorthand - skipping this record.
      continue;
    }

    const int label = fileEl.attribute("label").toInt();
    m_items.insert(Item(filePath, label));
  }
}

QDomElement FileNameDisambiguator::Impl::toXml(QDomDocument& doc,
                                               const QString& name,
                                               const std::function<QString(const QString&)>& filePathPacker) const {
  const QMutexLocker locker(&m_mutex);

  QDomElement el(doc.createElement(name));

  for (const Item& item : m_unorderedItems) {
    const QString filePathShorthand = filePathPacker(item.filePath);
    if (filePathShorthand.isEmpty()) {
      // Unrepresentable file path - skipping this record.
      continue;
    }

    QDomElement fileEl(doc.createElement("mapping"));
    fileEl.setAttribute("file", filePathShorthand);
    fileEl.setAttribute("label", item.label);
    el.appendChild(fileEl);
  }
  return el;
}

int FileNameDisambiguator::Impl::getLabel(const QString& filePath) const {
  const QMutexLocker locker(&m_mutex);

  const ItemsByFilePath::iterator fpIt(m_itemsByFilePath.find(filePath));
  if (fpIt != m_itemsByFilePath.end()) {
    return fpIt->label;
  }
  return 0;
}

int FileNameDisambiguator::Impl::registerFile(const QString& filePath) {
  const QMutexLocker locker(&m_mutex);

  const ItemsByFilePath::iterator fpIt(m_itemsByFilePath.find(filePath));
  if (fpIt != m_itemsByFilePath.end()) {
    return fpIt->label;
  }

  // Labels already given are kept (they are stored in the project), so the output names of
  // existing pages don't change; only the new file gets a label of its own.
  const int label = freeLabel(m_items, filePath);
  m_items.insert(Item(filePath, label));
  return label;
}

void FileNameDisambiguator::Impl::performRelinking(const AbstractRelinker& relinker) {
  const QMutexLocker locker(&m_mutex);
  Container newItems;

  // Files that stay where they are keep their labels. A moved or renamed file may now share its
  // output names with another one; it gets a new label then, instead of being dropped.
  std::vector<Item> movedItems;
  for (const Item& item : m_unorderedItems) {
    const QString newPath(relinker.substitutionPathFor(RelinkablePath(item.filePath, RelinkablePath::File)));
    if (newPath == item.filePath) {
      newItems.insert(item);
    } else {
      movedItems.emplace_back(newPath, item.label);
    }
  }
  for (const Item& item : movedItems) {
    if (newItems.get<ItemsByFilePathTag>().count(item.filePath) != 0) {
      continue;  // Two paths were relinked to the same file.
    }
    const bool labelStillFree = !hasOverlappingItem(newItems, item.filePath, item.label);
    newItems.insert(labelStillFree ? item : Item(item.filePath, freeLabel(newItems, item.filePath)));
  }

  m_items.swap(newItems);
}

bool FileNameDisambiguator::Impl::hasOverlappingItem(const Container& items, const QString& filePath, const int label) {
  // Case folded, as the file systems of Windows and macOS ignore case.
  const QString stem(outputStem(filePath, label).toCaseFolded());
  return std::any_of(items.begin(), items.end(), [&](const Item& item) {
    return (item.filePath != filePath) && stemsOverlap(stem, item.foldedBaseName + labelText(item.label));
  });
}

int FileNameDisambiguator::Impl::freeLabel(const Container& items, const QString& filePath) {
  int label = 0;
  while (hasOverlappingItem(items, filePath, label)) {
    ++label;
  }
  return label;
}

/*============================ Impl::Item =============================*/

FileNameDisambiguator::Impl::Item::Item(const QString& filePath, int lbl)
    : filePath(filePath),
      fileName(QFileInfo(filePath).fileName()),
      foldedBaseName(QFileInfo(filePath).completeBaseName().toCaseFolded()),
      label(lbl) {}
