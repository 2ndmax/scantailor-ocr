// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#define BOOST_AUTO_TEST_MAIN

#include <QCoreApplication>
#include <boost/test/unit_test.hpp>
#include <memory>

namespace {
/** QProcess and other Qt classes need an application object, for all tests alike. */
struct ApplicationFixture {
  ApplicationFixture() {
    static int argc = 1;
    static char arg0[] = "core_tests";
    static char* argv[] = {arg0, nullptr};
    app = std::make_unique<QCoreApplication>(argc, argv);
  }

  std::unique_ptr<QCoreApplication> app;
};
}  // namespace

BOOST_GLOBAL_FIXTURE(ApplicationFixture);
