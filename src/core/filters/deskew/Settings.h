// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DESKEW_SETTINGS_H_
#define SCANTAILOR_DESKEW_SETTINGS_H_

#include <DeviationProvider.h>

#include <QMutex>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>

#include "NonCopyable.h"
#include "PageId.h"
#include "Params.h"

class AbstractRelinker;
class ProjectReader;
class QDomElement;

namespace deskew {
class Settings {
  DECLARE_NON_COPYABLE(Settings)

 public:
  Settings();

  virtual ~Settings();

  void clear();

  /**
   * Replaces the settings with those of the project file's element of this filter.
   */
  void load(const ProjectReader& reader, const QDomElement& filterEl);

  void performRelinking(const AbstractRelinker& relinker);

  void setPageParams(const PageId& pageId, const Params& params);

  std::unique_ptr<Params> getPageParams(const PageId& pageId) const;

  bool isParamsNull(const PageId& pageId) const;

  const DeviationProvider<PageId>& deviationProvider() const;

  void setPendingAutoOblique(const PageId& pageId, bool enabled);

  std::optional<bool> takePendingAutoOblique(const PageId& pageId);

 private:
  using PerPageParams = std::unordered_map<PageId, Params>;

  mutable QMutex m_mutex;
  PerPageParams m_perPageParams;
  DeviationProvider<PageId> m_deviationProvider;
  std::unordered_map<PageId, bool> m_pendingAutoOblique;
};
}  // namespace deskew
#endif  // ifndef SCANTAILOR_DESKEW_SETTINGS_H_
