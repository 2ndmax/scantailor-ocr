// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PAGEORDEROPTION_H_
#define SCANTAILOR_CORE_PAGEORDEROPTION_H_

#include <QString>
#include <memory>

#include "PageOrderProvider.h"

class PageOrderOption {
  // Member-wise copying is OK.
 public:
  using ProviderPtr = std::shared_ptr<const PageOrderProvider>;

  PageOrderOption(const QString& name, ProviderPtr provider, const QString& toolTip = QString())
      : m_name(name), m_provider(std::move(provider)), m_toolTip(toolTip) {}

  const QString& name() const { return m_name; }

  /** Explains the order, if the name doesn't. */
  const QString& toolTip() const { return m_toolTip; }

  /**
   * Returns the ordering information provider.
   * A null provider is OK and is to be interpreted as default order.
   */
  const ProviderPtr& provider() const { return m_provider; }

 private:
  QString m_name;
  ProviderPtr m_provider;
  QString m_toolTip;
};


#endif  // ifndef SCANTAILOR_CORE_PAGEORDEROPTION_H_
