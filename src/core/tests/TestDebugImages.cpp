// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <DebugImagesImpl.h>

#include <QByteArray>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <boost/test/unit_test.hpp>
#include <vector>

namespace Tests {
namespace {
// Points the temporary folder of the process to \p path for as long as the object lives.
class TempFolderOverride {
 public:
  explicit TempFolderOverride(const QString& path) {
    for (const char* name : {"TMP", "TEMP", "TMPDIR"}) {
      m_previous.push_back({name, qEnvironmentVariableIsSet(name), qgetenv(name)});
      qputenv(name, path.toLocal8Bit());
    }
  }

  ~TempFolderOverride() {
    for (const Variable& variable : m_previous) {
      if (variable.wasSet) {
        qputenv(variable.name, variable.value);
      } else {
        qunsetenv(variable.name);
      }
    }
  }

 private:
  struct Variable {
    const char* name;
    bool wasSet;
    QByteArray value;
  };

  std::vector<Variable> m_previous;
};
}  // namespace

BOOST_AUTO_TEST_SUITE(DebugImagesTestSuite)

// Windows refuses to delete a file that is still open, so a failed write left it behind.
BOOST_AUTO_TEST_CASE(a_debug_image_that_cannot_be_written_leaves_no_file) {
  const QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const TempFolderOverride override(tempDir.path());

  DebugImagesImpl images;
  images.add(QImage(), "null image cannot be written", {});

  BOOST_CHECK(images.empty());
  BOOST_CHECK(QDir(tempDir.path()).isEmpty());
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
