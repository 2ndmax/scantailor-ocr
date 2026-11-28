// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include <BinaryImage.h>
#include <DebugImagesImpl.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
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

BOOST_AUTO_TEST_CASE(add_retrieve_clear_and_auto_remove_lifecycle) {
  const QTemporaryDir tempDir;
  BOOST_REQUIRE(tempDir.isValid());
  const TempFolderOverride override(tempDir.path());

  DebugImagesImpl images;
  BOOST_CHECK(images.empty());

  QImage img(16, 12, QImage::Format_RGB32);
  img.fill(Qt::white);
  const auto factory = [](const QImage&) -> QWidget* { return nullptr; };
  images.add(img, QStringLiteral("first"), factory);

  const imageproc::BinaryImage bin(8, 8, imageproc::WHITE);
  images.add(bin, QStringLiteral("second"));

  BOOST_CHECK(!images.empty());

  QString path1;
  QString path2;
  {
    QString label1;
    std::function<QWidget*(const QImage&)> retrievedFactory;
    const AutoRemovingFile file1 = images.retrieveNext(&label1, &retrievedFactory);
    BOOST_CHECK(!file1.get().isEmpty());
    BOOST_CHECK(QFile::exists(file1.get()));
    BOOST_CHECK(label1 == QStringLiteral("first"));
    BOOST_CHECK(static_cast<bool>(retrievedFactory));
    path1 = file1.get();
    BOOST_CHECK(!images.empty());

    QString label2;
    std::function<QWidget*(const QImage&)> missingFactory = factory;
    const AutoRemovingFile file2 = images.retrieveNext(&label2, &missingFactory);
    BOOST_CHECK(!file2.get().isEmpty());
    BOOST_CHECK(QFile::exists(file2.get()));
    BOOST_CHECK(label2 == QStringLiteral("second"));
    BOOST_CHECK(!missingFactory);
    path2 = file2.get();
    BOOST_CHECK(images.empty());

    const AutoRemovingFile emptyFile = images.retrieveNext();
    BOOST_CHECK(emptyFile.get().isEmpty());
  }

  BOOST_CHECK(!QFile::exists(path1));
  BOOST_CHECK(!QFile::exists(path2));
}

BOOST_AUTO_TEST_SUITE_END()
}  // namespace Tests
