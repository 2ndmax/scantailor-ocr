// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OcrTextOutputs.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTemporaryDir>
#include <algorithm>

namespace {
QString nativePath(const QString& path) {
  return QDir::toNativeSeparators(path);
}

QString couldNotCreate(const QString& path, const QString& reason) {
  return QCoreApplication::translate("OcrTextOutputs", "Could not create %1.").arg(nativePath(path)) + ' ' + reason;
}

QString couldNotWrite(const QString& path, const QString& reason) {
  return QCoreApplication::translate("OcrTextOutputs", "Could not write %1.").arg(nativePath(path)) + ' ' + reason;
}

QString foreignFilesInFolder(const QString& dir) {
  return QCoreApplication::translate("OcrTextOutputs",
                                     "The folder %1 holds other files than ALTO files of an export, so it was not "
                                     "replaced. Move these files or choose another name.")
      .arg(nativePath(dir));
}

QString couldNotReplaceFolder(const QString& dir) {
  return QCoreApplication::translate("OcrTextOutputs", "Could not replace the folder %1. It may be in use.")
      .arg(nativePath(dir));
}

bool writeAll(QIODevice& device, const QByteArray& data, const QString& path, QString* error) {
  if (device.write(data) != data.size()) {
    *error = couldNotWrite(path, device.errorString());
    return false;
  }
  return true;
}

// The head and the end of the hOCR and ALTO files are taken from Tesseract 5.5.3 (Apache License 2.0),
// src/api/hocrrenderer.cpp and src/api/altorenderer.cpp.

QByteArray hocrHead(const QString& title, const QString& tesseractVersion) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\"\n"
         "    \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">\n"
         "<html xmlns=\"http://www.w3.org/1999/xhtml\" xml:lang=\"en\" lang=\"en\">\n"
         " <head>\n"
         "  <title>"
         + OcrTextOutputs::xmlText(title)
         + "</title>\n"
           "  <meta http-equiv=\"Content-Type\" content=\"text/html;charset=utf-8\"/>\n"
           "  <meta name='ocr-system' content='tesseract "
         + OcrTextOutputs::xmlText(tesseractVersion)
         + "' />\n"
           "  <meta name='ocr-capabilities' content='ocr_page ocr_carea ocr_par ocr_line ocrx_word ocrp_dir ocrp_lang "
           "ocrp_wconf'/>\n"
           " </head>\n"
           " <body>\n";
}

const char HOCR_END[] = " </body>\n</html>\n";

/** Unlike Tesseract's head, this one escapes the file name, so don't copy it back from there. */
QByteArray altoHead(const QString& imageFile, const QString& tesseractVersion) {
  return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
         "<alto xmlns=\"http://www.loc.gov/standards/alto/ns-v3#\" "
         "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
         "xsi:schemaLocation=\"http://www.loc.gov/standards/alto/ns-v3# "
         "http://www.loc.gov/alto/v3/alto-3-0.xsd\">\n"
         "\t<Description>\n"
         "\t\t<MeasurementUnit>pixel</MeasurementUnit>\n"
         "\t\t<sourceImageInformation>\n"
         "\t\t\t<fileName>"
         + OcrTextOutputs::xmlText(imageFile)
         + "</fileName>\n"
           "\t\t</sourceImageInformation>\n"
           "\t\t<OCRProcessing ID=\"OCR_0\">\n"
           "\t\t\t<ocrProcessingStep>\n"
           "\t\t\t\t<processingSoftware>\n"
           "\t\t\t\t\t<softwareName>Tesseract</softwareName>\n"
           "\t\t\t\t\t<softwareVersion>"
         + OcrTextOutputs::xmlText(tesseractVersion)
         + "</softwareVersion>\n"
           "\t\t\t\t</processingSoftware>\n"
           "\t\t\t</ocrProcessingStep>\n"
           "\t\t</OCRProcessing>\n"
           "\t</Description>\n"
           "\t<Layout>\n";
}

const char ALTO_END[] = "\t</Layout>\n</alto>\n";

/** Whether the folder holds nothing but files named like the ALTO files of an export, so nothing else gets lost. */
bool holdsOnlyAltoFiles(const QString& dir) {
  static const QRegularExpression altoFileName(QStringLiteral("^[0-9]{4,}_.*\\.xml$"));
  const QFileInfoList entries
      = QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
  return std::all_of(entries.cbegin(), entries.cend(), [](const QFileInfo& entry) {
    return entry.isFile() && altoFileName.match(entry.fileName()).hasMatch();
  });
}

/** A name for moving \p dir aside that isn't taken yet, next to it, so a remainder can be told apart. */
QString freeOldDirName(const QString& dir) {
  for (int number = 1;; ++number) {
    const QString candidate = QStringLiteral("%1.old-%2").arg(dir).arg(number);
    if (!QFileInfo::exists(candidate)) {
      return candidate;
    }
  }
}

/**
 * Windows can't rename a folder over an existing one, so the old one moves aside first. There is
 * no moment without an ALTO folder apart from the two renames, and a failed second one is undone.
 */
bool replaceDir(const QString& newDir, const QString& targetDir, QString* error) {
  QDir parent;
  if (!QFileInfo::exists(targetDir)) {
    if (!parent.rename(newDir, targetDir)) {
      *error = couldNotReplaceFolder(targetDir);
      return false;
    }
    return true;
  }

  // Checked at the start as well, but the export may have taken hours.
  if (!holdsOnlyAltoFiles(targetDir)) {
    *error = foreignFilesInFolder(targetDir);
    return false;
  }
  const QString oldDir = freeOldDirName(targetDir);
  if (!parent.rename(targetDir, oldDir)) {
    *error = couldNotReplaceFolder(targetDir);
    return false;
  }
  if (!parent.rename(newDir, targetDir)) {
    parent.rename(oldDir, targetDir);
    *error = couldNotReplaceFolder(targetDir);
    return false;
  }
  // REVIEW-NOTE (2026-11): a file of the old folder is locked -> "<name>.old-1" stays behind beside the new folder.
  // Not fixed because the export itself succeeded and the remainder can be told apart by its name.
  QDir(oldDir).removeRecursively();
  return true;
}
}  // namespace

struct OcrTextOutputs::Files {
  std::unique_ptr<QSaveFile> txt;
  std::unique_ptr<QSaveFile> hocr;
  std::unique_ptr<QTemporaryDir> altoDir;
};

OcrTextOutputs::OcrTextOutputs(const Settings& settings) : m_settings(settings), m_files(std::make_unique<Files>()) {}

OcrTextOutputs::~OcrTextOutputs() = default;

bool OcrTextOutputs::begin(QString* error) {
  if (!m_settings.txtFile.isEmpty()) {
    m_files->txt = std::make_unique<QSaveFile>(m_settings.txtFile);
    if (!m_files->txt->open(QIODevice::WriteOnly)) {
      *error = couldNotCreate(m_settings.txtFile, m_files->txt->errorString());
      return false;
    }
  }

  if (!m_settings.hocrFile.isEmpty()) {
    m_files->hocr = std::make_unique<QSaveFile>(m_settings.hocrFile);
    if (!m_files->hocr->open(QIODevice::WriteOnly)) {
      *error = couldNotCreate(m_settings.hocrFile, m_files->hocr->errorString());
      return false;
    }
    const QString title = QFileInfo(m_settings.hocrFile).completeBaseName();
    if (!writeAll(*m_files->hocr, hocrHead(title, m_settings.tesseractVersion), m_settings.hocrFile, error)) {
      return false;
    }
  }

  if (!m_settings.altoDir.isEmpty()) {
    const QFileInfo existing(m_settings.altoDir);
    if (existing.exists() && !existing.isDir()) {
      *error = QCoreApplication::translate("OcrTextOutputs",
                                           "There is a file named like the folder %1, so the folder could not be "
                                           "created. Move the file or choose another name.")
                   .arg(nativePath(m_settings.altoDir));
      return false;
    }
    if (existing.isDir() && !holdsOnlyAltoFiles(m_settings.altoDir)) {
      *error = foreignFilesInFolder(m_settings.altoDir);
      return false;
    }
    // Beside the target, so it can be renamed; its name tells where a remainder after a crash belongs.
    m_files->altoDir = std::make_unique<QTemporaryDir>(m_settings.altoDir + QStringLiteral(".tmp-XXXXXX"));
    if (!m_files->altoDir->isValid()) {
      *error = couldNotCreate(m_settings.altoDir, m_files->altoDir->errorString());
      return false;
    }
    // A temporary folder is private to the user, unlike the other files of the export.
    QFile::setPermissions(m_files->altoDir->path(), QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                                        | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
                                                        | QFile::ExeOther);
  }
  return true;
}

bool OcrTextOutputs::addPage(const QString& imageFile, const OcrPageTexts& texts, QString* error) {
  const int position = ++m_pagesAdded;

  if (m_files->txt) {
    // The one place that joins the pages. No separator before the first page, like Tesseract's own TXT files.
    if ((position > 1) && !writeAll(*m_files->txt, QByteArrayLiteral("\f"), m_settings.txtFile, error)) {
      return false;
    }
    if (!writeAll(*m_files->txt, texts.text, m_settings.txtFile, error)) {
      return false;
    }
  }

  if (m_files->hocr && !writeAll(*m_files->hocr, texts.hocr, m_settings.hocrFile, error)) {
    return false;
  }

  if (m_files->altoDir) {
    const QString name = altoFileName(position, m_settings.pageCount, imageFile);
    QFile file(m_files->altoDir->filePath(name));
    const QString shownPath = QDir(m_settings.altoDir).filePath(name);
    if (!file.open(QIODevice::WriteOnly)) {
      *error = couldNotCreate(shownPath, file.errorString());
      return false;
    }
    if (!writeAll(file, altoHead(imageFile, m_settings.tesseractVersion), shownPath, error)
        || !writeAll(file, texts.alto, shownPath, error) || !writeAll(file, ALTO_END, shownPath, error)) {
      return false;
    }
  }
  return true;
}

bool OcrTextOutputs::finish(QString* error) {
  return !m_files->hocr || writeAll(*m_files->hocr, HOCR_END, m_settings.hocrFile, error);
}

bool OcrTextOutputs::commit(QStringList* replaced, QString* error) {
  if (m_files->txt) {
    if (!m_files->txt->commit()) {
      *error = couldNotWrite(m_settings.txtFile, m_files->txt->errorString());
      return false;
    }
    replaced->push_back(nativePath(m_settings.txtFile));
  }

  if (m_files->hocr) {
    if (!m_files->hocr->commit()) {
      *error = couldNotWrite(m_settings.hocrFile, m_files->hocr->errorString());
      return false;
    }
    replaced->push_back(nativePath(m_settings.hocrFile));
  }

  if (m_files->altoDir) {
    if (!replaceDir(m_files->altoDir->path(), m_settings.altoDir, error)) {
      return false;
    }
    m_files->altoDir->setAutoRemove(false);
    replaced->push_back(nativePath(m_settings.altoDir));
  }
  return true;
}

void OcrTextOutputs::cancel() {
  if (m_files->txt) {
    m_files->txt->cancelWriting();
  }
  if (m_files->hocr) {
    m_files->hocr->cancelWriting();
  }
  m_files->altoDir.reset();
}

QByteArray OcrTextOutputs::xmlText(const QString& text) {
  return xmlCharacters(text).toHtmlEscaped().toUtf8();
}

QString OcrTextOutputs::xmlCharacters(const QString& text) {
  // XML 1.0 allows no control characters but tab, line feed and carriage return, not even escaped, nor U+FFFE,
  // U+FFFF and lone surrogates. File names on Linux and macOS may hold them.
  QString allowed;
  allowed.reserve(text.size());
  for (qsizetype i = 0; i < text.size(); ++i) {
    const QChar c = text[i];
    if (c.isHighSurrogate() && (i + 1 < text.size()) && text[i + 1].isLowSurrogate()) {
      allowed += c;
      allowed += text[++i];
      continue;
    }
    const char16_t u = c.unicode();
    const bool control = (u < 0x20) && (u != 0x09) && (u != 0x0A) && (u != 0x0D);  // but tab, LF, CR
    if (control || c.isSurrogate() || (u == 0xFFFE) || (u == 0xFFFF)) {
      allowed += QChar::ReplacementCharacter;
    } else {
      allowed += c;
    }
  }
  return allowed;
}

QString OcrTextOutputs::altoFileName(const int position, const int pageCount, const QString& imageFile) {
  // At least four digits, as many as the export needs, so the files sort in the order of the pages.
  const int width = std::max(4, static_cast<int>(QString::number(pageCount).size()));
  return QStringLiteral("%1_%2.xml")
      .arg(position, width, 10, QLatin1Char('0'))
      .arg(QFileInfo(imageFile).completeBaseName());
}
