// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_ABSTRACTFILTER_H_
#define SCANTAILOR_CORE_ABSTRACTFILTER_H_

#include <cassert>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "PageOrderOption.h"
#include "PageView.h"


class PageInfo;
class ProjectIdReader;
class ProjectIdWriter;
class AbstractRelinker;
class QString;
class QDomDocument;
class QDomElement;

/**
 * Filters represent processing stages, like "Deskew", "Margins" and "Output".
 */
class AbstractFilter : public std::enable_shared_from_this<AbstractFilter> {
 public:
  virtual ~AbstractFilter() = default;

  virtual QString getName() const = 0;

  virtual PageView getView() const = 0;

  virtual void selected() {}

  /**
   * The index into pageOrderOptions(), or -1 if this filter offers no page orders.
   */
  int selectedPageOrder() const { return m_pageOrderOptions.empty() ? -1 : m_selectedPageOrder; }

  void selectPageOrder(int option) {
    // A filter without page orders has nothing to select.
    if (m_pageOrderOptions.empty()) {
      return;
    }
    assert((option >= 0) && (static_cast<size_t>(option) < m_pageOrderOptions.size()));
    if ((option < 0) || (static_cast<size_t>(option) >= m_pageOrderOptions.size())) {
      return;
    }
    m_selectedPageOrder = option;
  }

  const std::vector<PageOrderOption>& pageOrderOptions() const { return m_pageOrderOptions; }

  virtual void performRelinking(const AbstractRelinker& relinker) = 0;

  virtual QDomElement saveSettings(const ProjectIdWriter& writer, QDomDocument& doc) const = 0;

  virtual void loadSettings(const ProjectIdReader& reader, const QDomElement& filtersEl) = 0;


 protected:
  /**
   * Offers one more page order. The first one added is selected.
   */
  void addPageOrderOption(PageOrderOption option) { m_pageOrderOptions.push_back(std::move(option)); }

 private:
  std::vector<PageOrderOption> m_pageOrderOptions;
  int m_selectedPageOrder = 0;
};


#endif  // ifndef SCANTAILOR_CORE_ABSTRACTFILTER_H_
