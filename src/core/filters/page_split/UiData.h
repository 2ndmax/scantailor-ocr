// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_SPLIT_UIDATA_H_
#define SCANTAILOR_PAGE_SPLIT_UIDATA_H_

#include "AutoManualMode.h"
#include "Dependencies.h"
#include "PageLayout.h"

namespace page_split {
/**
 * What processing a page found, for the options panel to show.
 */
class UiData {
  // Member-wise copying is OK.
 public:
  UiData();

  ~UiData();

  void setPageLayout(const PageLayout& layout);

  const PageLayout& pageLayout() const;

  void setDependencies(const Dependencies& deps);

  const Dependencies& dependencies() const;

  void setSplitLineMode(AutoManualMode mode);

  AutoManualMode splitLineMode() const;

  bool layoutTypeAutoDetected() const;

  void setLayoutTypeAutoDetected(bool val);

 private:
  PageLayout m_pageLayout;
  Dependencies m_deps;
  AutoManualMode m_splitLineMode;
  bool m_layoutTypeAutoDetected;
};

inline void UiData::setPageLayout(const PageLayout& layout) {
  m_pageLayout = layout;
}

inline const PageLayout& UiData::pageLayout() const {
  return m_pageLayout;
}

inline void UiData::setDependencies(const Dependencies& deps) {
  m_deps = deps;
}

inline const Dependencies& UiData::dependencies() const {
  return m_deps;
}

inline void UiData::setSplitLineMode(const AutoManualMode mode) {
  m_splitLineMode = mode;
}

inline AutoManualMode UiData::splitLineMode() const {
  return m_splitLineMode;
}

inline bool UiData::layoutTypeAutoDetected() const {
  return m_layoutTypeAutoDetected;
}

inline void UiData::setLayoutTypeAutoDetected(const bool val) {
  m_layoutTypeAutoDetected = val;
}
}  // namespace page_split
#endif  // SCANTAILOR_PAGE_SPLIT_UIDATA_H_
