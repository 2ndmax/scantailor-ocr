// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DEFAULTPARAMSPROVIDER_H_
#define SCANTAILOR_CORE_DEFAULTPARAMSPROVIDER_H_

#include <foundation/NonCopyable.h>

#include <QMutex>
#include <QtCore/QString>
#include <memory>

#include "DefaultParams.h"

/**
 * Thread-safe: the parameters are read by worker threads, while the GUI
 * may replace them at any time.  That's why copies are returned rather
 * than references to the internal objects.
 */
class DefaultParamsProvider {
  DECLARE_NON_COPYABLE(DefaultParamsProvider)
 private:
  DefaultParamsProvider();

 public:
  static DefaultParamsProvider& getInstance();

  QString getProfileName() const;

  DefaultParams getParams() const;

  void setParams(std::unique_ptr<DefaultParams> params, const QString& name);

 private:
  mutable QMutex m_mutex;
  QString m_profileName;
  std::unique_ptr<DefaultParams> m_params;
};


#endif  // SCANTAILOR_CORE_DEFAULTPARAMSPROVIDER_H_
