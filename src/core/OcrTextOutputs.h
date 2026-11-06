// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_OCRTEXTOUTPUTS_H_
#define SCANTAILOR_CORE_OCRTEXTOUTPUTS_H_

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QtGlobal>
#include <memory>

#include "NonCopyable.h"

/** What Tesseract delivers for one page, in UTF-8. Only the requested parts are filled. */
struct OcrPageTexts {
  QByteArray text;
  QByteArray hocr;
  QByteArray alto;

  qint64 bytes() const { return text.size() + hocr.size() + alto.size(); }
};

/**
 * \brief Writes the text formats of an export: one TXT file, one hOCR file, a folder of ALTO files.
 *
 * Everything is written to temporary files until commit(), so a failed or canceled export leaves
 * no partial output behind and doesn't touch existing files. The calls are begin(), addPage() for
 * every page in the order of the export, finish() and commit(); cancel() may come at any time.
 *
 * Tesseract's own renderers aren't used: they open the files themselves in the ANSI code page on
 * Windows, and they expect to recognize the pages in order, while the export does so in parallel.
 */
class OcrTextOutputs {
  DECLARE_NON_COPYABLE(OcrTextOutputs)

 public:
  struct Settings {
    /** Where the formats go; an empty path means the format isn't created. */
    QString txtFile;
    QString hocrFile;
    QString altoDir;
    /** The version of Tesseract, named in the hOCR and ALTO files. */
    QString tesseractVersion;
    /** The number of pages of the export, which sets the width of the numbers of the ALTO files. */
    int pageCount = 0;
  };

  explicit OcrTextOutputs(const Settings& settings);

  ~OcrTextOutputs();

  /** Opens the temporary files. Fails if an existing ALTO folder holds anything but ALTO files of an export. */
  bool begin(QString* error);

  /** Adds the next page. \p imageFile is the name of its image, without a folder. */
  bool addPage(const QString& imageFile, const OcrPageTexts& texts, QString* error);

  /** Completes the temporary files. */
  bool finish(QString* error);

  /**
   * Puts the files in place of existing ones. \p replaced receives each output that was put in
   * place, so after a failure the caller can tell which ones are new already.
   */
  bool commit(QStringList* replaced, QString* error);

  /** Discards the temporary files. */
  void cancel();

  /** The name of the ALTO file of a page, e.g. "0001_scan_017.xml". \p position counts from 1. */
  static QString altoFileName(int position, int pageCount, const QString& imageFile);

  /** \p text for the content of an XML element or attribute, in UTF-8. */
  static QByteArray xmlText(const QString& text);

  /** \p text with the characters XML doesn't allow, not even escaped, replaced by U+FFFD; not escaped. */
  static QString xmlCharacters(const QString& text);

 private:
  struct Files;

  Settings m_settings;
  std::unique_ptr<Files> m_files;
  int m_pagesAdded = 0;
};

#endif  // SCANTAILOR_CORE_OCRTEXTOUTPUTS_H_
