// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_AUTOREMOVINGFILE_H_
#define SCANTAILOR_FOUNDATION_AUTOREMOVINGFILE_H_

#include <QString>

/**
 * \brief Provides move-only unique_ptr semantics for files.
 *
 * Just like std::unique_ptr deleting its object when it goes out of scope,
 * this class deletes a file.
 */
class AutoRemovingFile {
 public:
  AutoRemovingFile();

  explicit AutoRemovingFile(const QString& filePath);

  AutoRemovingFile(const AutoRemovingFile&) = delete;

  AutoRemovingFile(AutoRemovingFile&& other) noexcept;

  ~AutoRemovingFile();

  AutoRemovingFile& operator=(const AutoRemovingFile&) = delete;

  AutoRemovingFile& operator=(AutoRemovingFile&& other) noexcept;

  const QString& get() const { return m_file; }

  void reset(const QString& file);

  QString release();

 private:
  QString m_file;
};


#endif  // ifndef SCANTAILOR_FOUNDATION_AUTOREMOVINGFILE_H_
