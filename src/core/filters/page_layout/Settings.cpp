// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Settings.h"

#include <DeviationProvider.h>

#include <QMutex>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/sequenced_index.hpp>
#include <boost/multi_index_container.hpp>
#include <utility>

#include "AbstractRelinker.h"
#include "Guide.h"
#include "PageId.h"
#include "PageSequence.h"
#include "Params.h"
#include "RelinkedKeys.h"


using namespace ::boost;
using namespace ::boost::multi_index;

namespace page_layout {
class Settings::Item {
 public:
  PageId pageId;
  Margins hardMarginsMM;
  QRectF pageRect;
  QRectF contentRect;
  QSizeF contentSizeMM;
  Alignment alignment;
  bool autoMargins;

  Item(const PageId& pageId,
       const Margins& hardMarginsMm,
       const QRectF& pageRect,
       const QRectF& contentRect,
       const QSizeF& contentSizeMm,
       const Alignment& alignment,
       bool autoMargins);

  double hardWidthMM() const;

  double hardHeightMM() const;

  double influenceHardWidthMM() const;

  double influenceHardHeightMM() const;

  bool alignedWithOthers() const { return !alignment.isNull(); }
};


class Settings::Impl {
 public:
  Impl();

  ~Impl();

  void clear();

  void performRelinking(const AbstractRelinker& relinker);

  void removePagesMissingFrom(const PageSequence& pages);

  bool checkEverythingDefined(const PageSequence& pages, const PageId* ignore) const;

  std::unique_ptr<Params> getPageParams(const PageId& pageId) const;

  bool isParamsNull(const PageId& pageId) const;

  void setPageParams(const PageId& pageId, const Params& params);

  Params updateContentSizeAndGetParams(const PageId& pageId,
                                       const QRectF& pageRect,
                                       const QRectF& contentRect,
                                       const QSizeF& contentSizeMm,
                                       QSizeF* aggHardSizeBefore,
                                       QSizeF* aggHardSizeAfter);

  Margins getHardMarginsMM(const PageId& pageId) const;

  void setHardMarginsMM(const PageId& pageId, const Margins& marginsMm);

  Alignment getPageAlignment(const PageId& pageId) const;

  AggregateSizeChanged setPageAlignment(const PageId& pageId, const Alignment& alignment);

  AggregateSizeChanged setContentSizeMM(const PageId& pageId, const QSizeF& contentSizeMm);

  void invalidateContentSize(const PageId& pageId);

  QSizeF getAggregateHardSizeMM() const;

  QSizeF computeAggregateHardSizeMMLocked() const;

  QSizeF getAggregateHardSizeMM(const PageId& pageId, const QSizeF& hardSizeMm, const Alignment& alignment) const;

  bool isPageAutoMarginsEnabled(const PageId& pageId);

  void setPageAutoMarginsEnabled(const PageId& pageId, bool state);

  const DeviationProvider<PageId>& deviationProvider() const;

  std::vector<Guide>& guides();

  OutputSizeOptions outputSizeOptions() const {
    const QMutexLocker locker(&m_mutex);
    return m_outputSizeOptions;
  }

  void setOutputSizeOptions(const OutputSizeOptions& options) {
    const QMutexLocker locker(&m_mutex);
    m_outputSizeOptions = options;
  }

 private:
  class SequencedTag;
  class DescWidthTag;
  class DescHeightTag;

  using Container = multi_index_container<
      Item,
      indexed_by<hashed_unique<member<Item, PageId, &Item::pageId>, std::hash<PageId>>,
                 sequenced<tag<SequencedTag>>,
                 ordered_non_unique<tag<DescWidthTag>,
                                    // ORDER BY alignedWithOthers DESC, hardWidthMM DESC
                                    composite_key<Item,
                                                  const_mem_fun<Item, bool, &Item::alignedWithOthers>,
                                                  const_mem_fun<Item, double, &Item::hardWidthMM>>,
                                    composite_key_compare<std::greater<>, std::greater<double>>>,
                 ordered_non_unique<tag<DescHeightTag>,
                                    // ORDER BY alignedWithOthers DESC, hardHeightMM DESC
                                    composite_key<Item,
                                                  const_mem_fun<Item, bool, &Item::alignedWithOthers>,
                                                  const_mem_fun<Item, double, &Item::hardHeightMM>>,
                                    composite_key_compare<std::greater<>, std::greater<double>>>>>;

  using UnorderedItems = Container::index<SequencedTag>::type;
  using DescWidthOrder = Container::index<DescWidthTag>::type;
  using DescHeightOrder = Container::index<DescHeightTag>::type;

  /**
   * Applies p modify to the page's item. A page without one gets an item with the default
   * values, changed by p modify before it is inserted. Expects m_mutex to be locked.
   */
  template <typename Modifier>
  Container::iterator modifyOrInsertLocked(const PageId& pageId, Modifier modify);

  mutable QMutex m_mutex;
  Container m_items;
  UnorderedItems& m_unorderedItems;
  DescWidthOrder& m_descWidthOrder;
  DescHeightOrder& m_descHeightOrder;
  const QRectF m_invalidRect;
  const QSizeF m_invalidSize;
  const Margins m_defaultHardMarginsMM;
  const Alignment m_defaultAlignment;
  const bool m_autoMarginsDefault;
  DeviationProvider<PageId> m_deviationProvider;
  std::vector<Guide> m_guides;
  OutputSizeOptions m_outputSizeOptions;
};


/*=============================== Settings ==================================*/

Settings::Settings() : m_impl(std::make_unique<Impl>()) {}

Settings::~Settings() = default;

void Settings::clear() {
  return m_impl->clear();
}

void Settings::performRelinking(const AbstractRelinker& relinker) {
  m_impl->performRelinking(relinker);
}

void Settings::removePagesMissingFrom(const PageSequence& pages) {
  m_impl->removePagesMissingFrom(pages);
}

bool Settings::checkEverythingDefined(const PageSequence& pages, const PageId* ignore) const {
  return m_impl->checkEverythingDefined(pages, ignore);
}

std::unique_ptr<Params> Settings::getPageParams(const PageId& pageId) const {
  return m_impl->getPageParams(pageId);
}

void Settings::setPageParams(const PageId& pageId, const Params& params) {
  return m_impl->setPageParams(pageId, params);
}

Params Settings::updateContentSizeAndGetParams(const PageId& pageId,
                                               const QRectF& pageRect,
                                               const QRectF& contentRect,
                                               const QSizeF& contentSizeMm,
                                               QSizeF* aggHardSizeBefore,
                                               QSizeF* aggHardSizeAfter) {
  return m_impl->updateContentSizeAndGetParams(pageId, pageRect, contentRect, contentSizeMm, aggHardSizeBefore,
                                               aggHardSizeAfter);
}

Margins Settings::getHardMarginsMM(const PageId& pageId) const {
  return m_impl->getHardMarginsMM(pageId);
}

void Settings::setHardMarginsMM(const PageId& pageId, const Margins& marginsMm) {
  m_impl->setHardMarginsMM(pageId, marginsMm);
}

Alignment Settings::getPageAlignment(const PageId& pageId) const {
  return m_impl->getPageAlignment(pageId);
}

Settings::AggregateSizeChanged Settings::setPageAlignment(const PageId& pageId, const Alignment& alignment) {
  return m_impl->setPageAlignment(pageId, alignment);
}

Settings::AggregateSizeChanged Settings::setContentSizeMM(const PageId& pageId, const QSizeF& contentSizeMm) {
  return m_impl->setContentSizeMM(pageId, contentSizeMm);
}

void Settings::invalidateContentSize(const PageId& pageId) {
  return m_impl->invalidateContentSize(pageId);
}

QSizeF Settings::getAggregateHardSizeMM() const {
  return m_impl->getAggregateHardSizeMM();
}

QSizeF Settings::getAggregateHardSizeMM(const PageId& pageId,
                                        const QSizeF& hardSizeMm,
                                        const Alignment& alignment) const {
  return m_impl->getAggregateHardSizeMM(pageId, hardSizeMm, alignment);
}


bool Settings::isPageAutoMarginsEnabled(const PageId& pageId) {
  return m_impl->isPageAutoMarginsEnabled(pageId);
}

void Settings::setPageAutoMarginsEnabled(const PageId& pageId, const bool state) {
  return m_impl->setPageAutoMarginsEnabled(pageId, state);
}

bool Settings::isParamsNull(const PageId& pageId) const {
  return m_impl->isParamsNull(pageId);
}

const DeviationProvider<PageId>& Settings::deviationProvider() const {
  return m_impl->deviationProvider();
}

std::vector<Guide>& Settings::guides() {
  return m_impl->guides();
}

OutputSizeOptions Settings::outputSizeOptions() const {
  return m_impl->outputSizeOptions();
}

void Settings::setOutputSizeOptions(const OutputSizeOptions& options) {
  m_impl->setOutputSizeOptions(options);
}

/*============================== Settings::Item =============================*/

Settings::Item::Item(const PageId& pageId,
                     const Margins& hardMarginsMm,
                     const QRectF& pageRect,
                     const QRectF& contentRect,
                     const QSizeF& contentSizeMm,
                     const Alignment& alignment,
                     const bool autoMargins)
    : pageId(pageId),
      hardMarginsMM(hardMarginsMm),
      pageRect(pageRect),
      contentRect(contentRect),
      contentSizeMM(contentSizeMm),
      alignment(alignment),
      autoMargins(autoMargins) {}

double Settings::Item::hardWidthMM() const {
  return contentSizeMM.width() + hardMarginsMM.left() + hardMarginsMM.right();
}

double Settings::Item::hardHeightMM() const {
  return contentSizeMM.height() + hardMarginsMM.top() + hardMarginsMM.bottom();
}

double Settings::Item::influenceHardWidthMM() const {
  return alignment.isNull() ? 0.0 : hardWidthMM();
}

double Settings::Item::influenceHardHeightMM() const {
  return alignment.isNull() ? 0.0 : hardHeightMM();
}

/*============================= Settings::Impl ==============================*/

Settings::Impl::Impl()
    : m_items(),
      m_unorderedItems(m_items.get<SequencedTag>()),
      m_descWidthOrder(m_items.get<DescWidthTag>()),
      m_descHeightOrder(m_items.get<DescHeightTag>()),
      m_invalidRect(),
      m_invalidSize(),
      m_defaultHardMarginsMM(Margins(10.0, 5.0, 10.0, 5.0)),
      m_defaultAlignment(Alignment::TOP, Alignment::HCENTER),
      m_autoMarginsDefault(false),
      m_outputSizeOptions() {
  m_deviationProvider.setComputeValueByKey([this](const PageId& pageId) -> double {
    auto it = m_items.find(pageId);
    if (it != m_items.end()) {
      const Margins& marginsMm = it->hardMarginsMM;
      double horHardMargins = marginsMm.left() + marginsMm.right();
      double vertHardMargins = marginsMm.top() + marginsMm.bottom();
      return std::sqrt(std::pow(horHardMargins, 2) + std::pow(vertHardMargins, 2));
    }
    return NAN;
  });
}

Settings::Impl::~Impl() = default;

template <typename Modifier>
Settings::Impl::Container::iterator Settings::Impl::modifyOrInsertLocked(const PageId& pageId, Modifier modify) {
  const Container::iterator it(m_items.find(pageId));
  if (it != m_items.end()) {
    m_items.modify(it, modify);
    return it;
  }
  // The item is complete before it is inserted, so it takes its place in the ordered indexes once.
  Item item(pageId, m_defaultHardMarginsMM, m_invalidRect, m_invalidRect, m_invalidSize, m_defaultAlignment,
            m_autoMarginsDefault);
  modify(item);
  return m_items.insert(it, item);
}

namespace {
Settings::AggregateSizeChanged aggregateSizeChange(const QSizeF& before, const QSizeF& after) {
  return (before == after) ? Settings::AGGREGATE_SIZE_UNCHANGED : Settings::AGGREGATE_SIZE_CHANGED;
}
}  // namespace

void Settings::Impl::clear() {
  const QMutexLocker locker(&m_mutex);
  m_items.clear();
  m_deviationProvider.clear();
  // A loaded project sets its own afterwards, a new one takes them from its start values.
  m_outputSizeOptions = OutputSizeOptions();
  m_guides.clear();
}

void Settings::Impl::performRelinking(const AbstractRelinker& relinker) {
  QMutexLocker locker(&m_mutex);
  Container newItems;

  for (const Item& item : m_unorderedItems) {
    Item newItem(item);
    newItem.pageId = relinkedKey(item.pageId, relinker);
    // REVIEW-NOTE (2026-10): insert() fails silently for a second item with the same new page id -> one of
    // two pages relinked onto the same file loses margins and alignment. Not fixed because such a project
    // already contains the same page twice.
    newItems.insert(newItem);
  }

  m_items.swap(newItems);

  m_deviationProvider.clear();
  for (const Item& item : m_unorderedItems) {
    m_deviationProvider.addOrUpdate(item.pageId);
  }
}

void Settings::Impl::removePagesMissingFrom(const PageSequence& pages) {
  const QMutexLocker locker(&m_mutex);

  std::vector<PageId> sortedPages;

  sortedPages.reserve(pages.numPages());
  for (const PageInfo& page : pages) {
    sortedPages.push_back(page.id());
  }
  std::sort(sortedPages.begin(), sortedPages.end());

  UnorderedItems::const_iterator it(m_unorderedItems.begin());
  const UnorderedItems::const_iterator end(m_unorderedItems.end());
  while (it != end) {
    if (std::binary_search(sortedPages.begin(), sortedPages.end(), it->pageId)) {
      ++it;
    } else {
      m_deviationProvider.remove(it->pageId);
      m_unorderedItems.erase(it++);
    }
  }
}

bool Settings::Impl::checkEverythingDefined(const PageSequence& pages, const PageId* ignore) const {
  const QMutexLocker locker(&m_mutex);

  for (const PageInfo& pageInfo : pages) {
    if (ignore && (*ignore == pageInfo.id())) {
      continue;
    }
    const Container::iterator it(m_items.find(pageInfo.id()));
    if ((it == m_items.end()) || !it->contentSizeMM.isValid()) {
      return false;
    }
  }
  return true;
}

std::unique_ptr<Params> Settings::Impl::getPageParams(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);

  const Container::iterator it(m_items.find(pageId));
  if (it == m_items.end()) {
    return nullptr;
  }
  return std::make_unique<Params>(it->hardMarginsMM, it->pageRect, it->contentRect, it->contentSizeMM, it->alignment,
                                  it->autoMargins);
}

void Settings::Impl::setPageParams(const PageId& pageId, const Params& params) {
  const QMutexLocker locker(&m_mutex);

  const Item newItem(pageId, params.hardMarginsMM(), params.pageRect(), params.contentRect(), params.contentSizeMM(),
                     params.alignment(), params.isAutoMarginsEnabled());

  const Container::iterator it(m_items.find(pageId));
  if (it == m_items.end()) {
    m_items.insert(it, newItem);
  } else {
    m_items.replace(it, newItem);
  }

  m_deviationProvider.addOrUpdate(pageId);
}

Params Settings::Impl::updateContentSizeAndGetParams(const PageId& pageId,
                                                     const QRectF& pageRect,
                                                     const QRectF& contentRect,
                                                     const QSizeF& contentSizeMm,
                                                     QSizeF* aggHardSizeBefore,
                                                     QSizeF* aggHardSizeAfter) {
  const QMutexLocker locker(&m_mutex);

  if (aggHardSizeBefore) {
    *aggHardSizeBefore = computeAggregateHardSizeMMLocked();
  }

  const Container::iterator itemIt = modifyOrInsertLocked(pageId, [&](Item& item) {
    item.contentSizeMM = contentSizeMm;
    item.contentRect = contentRect;
    item.pageRect = pageRect;
  });

  if (aggHardSizeAfter) {
    *aggHardSizeAfter = computeAggregateHardSizeMMLocked();
  }

  m_deviationProvider.addOrUpdate(pageId);
  return Params(itemIt->hardMarginsMM, itemIt->pageRect, itemIt->contentRect, itemIt->contentSizeMM, itemIt->alignment,
                itemIt->autoMargins);
}  // Settings::Impl::updateContentSizeAndGetParams

Margins Settings::Impl::getHardMarginsMM(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);

  const Container::iterator it(m_items.find(pageId));
  if (it == m_items.end()) {
    return m_defaultHardMarginsMM;
  } else {
    return it->hardMarginsMM;
  }
}

void Settings::Impl::setHardMarginsMM(const PageId& pageId, const Margins& marginsMm) {
  const QMutexLocker locker(&m_mutex);

  modifyOrInsertLocked(pageId, [&](Item& item) { item.hardMarginsMM = marginsMm; });
  m_deviationProvider.addOrUpdate(pageId);
}

Alignment Settings::Impl::getPageAlignment(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);

  const Container::iterator it(m_items.find(pageId));
  if (it == m_items.end()) {
    return m_defaultAlignment;
  } else {
    return it->alignment;
  }
}

Settings::AggregateSizeChanged Settings::Impl::setPageAlignment(const PageId& pageId, const Alignment& alignment) {
  const QMutexLocker locker(&m_mutex);

  const QSizeF aggSizeBefore(computeAggregateHardSizeMMLocked());
  modifyOrInsertLocked(pageId, [&](Item& item) { item.alignment = alignment; });
  m_deviationProvider.addOrUpdate(pageId);
  return aggregateSizeChange(aggSizeBefore, computeAggregateHardSizeMMLocked());
}

Settings::AggregateSizeChanged Settings::Impl::setContentSizeMM(const PageId& pageId, const QSizeF& contentSizeMm) {
  const QMutexLocker locker(&m_mutex);

  const QSizeF aggSizeBefore(computeAggregateHardSizeMMLocked());
  // The page rect stays; the content rect no longer fits the new size.
  modifyOrInsertLocked(pageId, [&](Item& item) {
    item.contentSizeMM = contentSizeMm;
    item.contentRect = m_invalidRect;
  });
  m_deviationProvider.addOrUpdate(pageId);
  return aggregateSizeChange(aggSizeBefore, computeAggregateHardSizeMMLocked());
}

void Settings::Impl::invalidateContentSize(const PageId& pageId) {
  const QMutexLocker locker(&m_mutex);

  const Container::iterator it(m_items.find(pageId));
  if (it != m_items.end()) {
    m_items.modify(it, [&](Item& item) {
      item.contentSizeMM = m_invalidSize;
      item.contentRect = m_invalidRect;
    });
  }

  m_deviationProvider.addOrUpdate(pageId);
}

QSizeF Settings::Impl::getAggregateHardSizeMM() const {
  const QMutexLocker locker(&m_mutex);
  return computeAggregateHardSizeMMLocked();
}

QSizeF Settings::Impl::computeAggregateHardSizeMMLocked() const {
  if (m_items.empty()) {
    return QSizeF(0.0, 0.0);
  }

  const Item& maxWidthItem = *m_descWidthOrder.begin();
  const Item& maxHeightItem = *m_descHeightOrder.begin();

  const double width = maxWidthItem.influenceHardWidthMM();
  const double height = maxHeightItem.influenceHardHeightMM();
  return QSizeF(width, height);
}

QSizeF Settings::Impl::getAggregateHardSizeMM(const PageId& pageId,
                                              const QSizeF& hardSizeMm,
                                              const Alignment& alignment) const {
  const QMutexLocker locker(&m_mutex);
  if (alignment.isNull()) {
    return computeAggregateHardSizeMMLocked();
  }

  if (m_items.empty()) {
    return QSizeF(0.0, 0.0);
  }

  double width = 0.0;

  {
    DescWidthOrder::iterator it(m_descWidthOrder.begin());
    if (it->pageId != pageId) {
      width = it->influenceHardWidthMM();
    } else {
      ++it;
      if (it == m_descWidthOrder.end()) {
        width = hardSizeMm.width();
      } else {
        width = std::max(hardSizeMm.width(), qreal(it->influenceHardWidthMM()));
      }
    }
  }

  double height = 0.0;

  {
    DescHeightOrder::iterator it(m_descHeightOrder.begin());
    if (it->pageId != pageId) {
      height = it->influenceHardHeightMM();
    } else {
      ++it;
      if (it == m_descHeightOrder.end()) {
        height = hardSizeMm.height();
      } else {
        height = std::max(hardSizeMm.height(), qreal(it->influenceHardHeightMM()));
      }
    }
  }
  return QSizeF(width, height);
}  // Settings::Impl::getAggregateHardSizeMM

bool Settings::Impl::isPageAutoMarginsEnabled(const PageId& pageId) {
  const QMutexLocker locker(&m_mutex);

  const Container::iterator it(m_items.find(pageId));
  if (it == m_items.end()) {
    return m_autoMarginsDefault;
  } else {
    return it->autoMargins;
  }
}

void Settings::Impl::setPageAutoMarginsEnabled(const PageId& pageId, const bool state) {
  const QMutexLocker locker(&m_mutex);

  modifyOrInsertLocked(pageId, [&](Item& item) { item.autoMargins = state; });
}

bool Settings::Impl::isParamsNull(const PageId& pageId) const {
  const QMutexLocker locker(&m_mutex);
  return (m_items.find(pageId) == m_items.end());
}

const DeviationProvider<PageId>& Settings::Impl::deviationProvider() const {
  return m_deviationProvider;
}

std::vector<Guide>& Settings::Impl::guides() {
  // REVIEW-NOTE (2026-10): returns a mutable reference without holding m_mutex -> concurrent save and sync
  // would race. Not fixed because all callers run in the GUI thread.
  return m_guides;
}
}  // namespace page_layout