// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PAGEORDERPROVIDER_H_
#define SCANTAILOR_CORE_PAGEORDERPROVIDER_H_

#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "PageId.h"

/**
 * A base interface for different page ordering strategies.
 */
class PageOrderProvider : public std::enable_shared_from_this<PageOrderProvider> {
 public:
  /** Compares two pages like precedes(). */
  using Comparator
      = std::function<bool(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete)>;

  virtual ~PageOrderProvider() = default;

  /**
   * Returns true if \p lhsPage precedes \p rhsPage.
   * \p lhsIncomplete and \p rhsIncomplete indicate whether
   * a page is represented by IncompleteThumbnail.
   */
  virtual bool precedes(const PageId& lhsPage, bool lhsIncomplete, const PageId& rhsPage, bool rhsIncomplete) const = 0;

  /**
   * For sorting many pages: a comparator that orders like precedes(), but reads the settings of
   * each of \p pages only once, here. Sorting must not compare against settings that a processing
   * thread changes meanwhile, as an order that changes while sorting is undefined behavior.
   * The default compares with precedes(). The comparator must not outlive this object.
   */
  virtual Comparator snapshot(const std::vector<PageId>& pages) const;

  virtual std::shared_ptr<const PageOrderProvider> reversed() const;

 protected:
  /**
   * For snapshot(): stores valueOf(page) for each of \p pages, and compares the stored values with
   * compare(lhsValue, lhsIncomplete, lhsPage, rhsValue, rhsIncomplete, rhsPage). A page not in
   * \p pages is read when it is compared.
   */
  template <typename Value, typename ValueOf, typename Compare>
  static Comparator snapshotOf(const std::vector<PageId>& pages, ValueOf valueOf, Compare compare) {
    auto values = std::make_shared<std::map<PageId, Value>>();
    for (const PageId& page : pages) {
      values->emplace(page, valueOf(page));
    }
    return [values, valueOf, compare](const PageId& lhsPage, const bool lhsIncomplete, const PageId& rhsPage,
                                      const bool rhsIncomplete) {
      const auto valueAt = [&](const PageId& page) {
        const auto it = values->find(page);
        return (it != values->end()) ? it->second : Value(valueOf(page));
      };
      return compare(valueAt(lhsPage), lhsIncomplete, lhsPage, valueAt(rhsPage), rhsIncomplete, rhsPage);
    };
  }
};

#endif
