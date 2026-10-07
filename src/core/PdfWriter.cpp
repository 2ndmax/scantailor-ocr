// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfWriter.h"

#include <QDateTime>
#include <QIODevice>
#include <QUuid>
#include <cmath>

#include "GlyphLessFont.h"

PdfWriter::PdfWriter(QIODevice& device) : m_device(device), m_offsets(1, 0) {}

bool PdfWriter::begin() {
  // The second line contains bytes above 127, so that tools treat the file as binary.
  write("%PDF-1.4\n%\xE2\xE3\xCF\xD3\n");
  m_catalogId = allocateObject();
  m_pagesId = allocateObject();
  return m_ok;
}

bool PdfWriter::addPage(const Page& page) {
  const double pageWidth = page.size.width();
  const double pageHeight = page.size.height();

  QByteArray content;
  QByteArray xobjects;
  for (size_t i = 0; i < page.images.size(); ++i) {
    const Image& image = page.images[i];
    const QByteArray name = "/Im" + QByteArray::number(static_cast<qulonglong>(i));

    const int imageId = allocateObject();
    QByteArray dict = "/Type /XObject /Subtype /Image /Width " + QByteArray::number(image.width) + " /Height "
                      + QByteArray::number(image.height);
    const bool bitonalFilter
        = (image.encoding == Image::Encoding::CCITT_G4) || (image.encoding == Image::Encoding::JBIG2);
    const int bitsPerComponent = bitonalFilter                               ? 1
                                 : (image.encoding == Image::Encoding::JPEG) ? 8
                                                                             : image.bitsPerComponent;
    if (image.isMask) {
      // Decoded black pixels are 0 with all filters used, which an image mask paints with the fill colour.
      dict += " /ImageMask true /BitsPerComponent 1";
    } else {
      if (!image.palette.isEmpty() && !bitonalFilter && (image.encoding != Image::Encoding::JPEG)) {
        const int colorCount = static_cast<int>(image.palette.size() / 3);
        dict += " /ColorSpace [/Indexed /DeviceRGB " + QByteArray::number(colorCount - 1) + " <" + image.palette.toHex()
                + ">]";
      } else if (!bitonalFilter && (image.components == 3)) {
        dict += " /ColorSpace /DeviceRGB";
      } else {
        dict += " /ColorSpace /DeviceGray";
      }
      dict += " /BitsPerComponent " + QByteArray::number(bitsPerComponent);
    }
    switch (image.encoding) {
      case Image::Encoding::CCITT_G4:
        dict += " /Filter /CCITTFaxDecode /DecodeParms << /K -1 /Columns " + QByteArray::number(image.width) + " /Rows "
                + QByteArray::number(image.height) + " >>";
        break;
      case Image::Encoding::JBIG2:
        dict += " /Filter /JBIG2Decode";
        break;
      case Image::Encoding::JPEG:
        dict += " /Filter /DCTDecode";
        break;
      case Image::Encoding::FLATE:
        dict += " /Filter /FlateDecode";
        if (image.pngPredictors) {
          const int colors = image.palette.isEmpty() ? image.components : 1;
          dict += " /DecodeParms << /Predictor 15 /Colors " + QByteArray::number(colors) + " /BitsPerComponent "
                  + QByteArray::number(bitsPerComponent) + " /Columns " + QByteArray::number(image.width) + " >>";
        }
        break;
      case Image::Encoding::RAW:
        break;
    }
    writeStreamObject(imageId, dict, image.data);
    xobjects += name + ' ' + QByteArray::number(imageId) + " 0 R ";

    // PDF's origin is the bottom left corner of the page.
    const QRectF& r = image.rect;
    content += "q\n";
    if (image.isMask) {
      content += "0 g\n";
    }
    content += formatNumber(r.width()) + " 0 0 " + formatNumber(r.height()) + ' ' + formatNumber(r.left()) + ' '
               + formatNumber(pageHeight - r.bottom()) + " cm\n" + name + " Do\nQ\n";
  }

  QByteArray fonts;
  if (!page.words.empty()) {
    writeFont();
    fonts = "/Font << /F0 " + QByteArray::number(m_fontId) + " 0 R >> ";
    content += textContent(page);
  }

  const int contentId = allocateObject();
  // qCompress() prepends the uncompressed size as 4 bytes, the rest is a zlib stream.
  writeStreamObject(contentId, "/Filter /FlateDecode", qCompress(content, 9).mid(4));

  const int pageId = allocateObject();
  beginObject(pageId);
  write("<< /Type /Page /Parent " + QByteArray::number(m_pagesId) + " 0 R /MediaBox [0 0 " + formatNumber(pageWidth)
        + ' ' + formatNumber(pageHeight) + "] /Resources << /XObject << " + xobjects + ">> " + fonts + ">> /Contents "
        + QByteArray::number(contentId) + " 0 R >>\nendobj\n");
  m_pageIds.push_back(pageId);
  return m_ok;
}

bool PdfWriter::finish() {
  QByteArray kids;
  for (const int id : m_pageIds) {
    kids += QByteArray::number(id) + " 0 R ";
  }
  beginObject(m_pagesId);
  write("<< /Type /Pages /Kids [ " + kids + "] /Count " + QByteArray::number(pageCount()) + " >>\nendobj\n");

  beginObject(m_catalogId);
  write("<< /Type /Catalog /Pages " + QByteArray::number(m_pagesId) + " 0 R >>\nendobj\n");

  const int infoId = allocateObject();
  beginObject(infoId);
  QByteArray info = "<< ";
  if (!m_producer.isEmpty()) {
    info += "/Producer " + textString(m_producer) + ' ';
  }
  info += "/CreationDate (D:" + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmss").toLatin1() + "Z) >>";
  write(info + "\nendobj\n");

  const qint64 xrefOffset = m_written;
  const int objectCount = static_cast<int>(m_offsets.size());
  QByteArray xref = "xref\n0 " + QByteArray::number(objectCount) + "\n0000000000 65535 f \n";
  for (int id = 1; id < objectCount; ++id) {
    // Each entry has to be exactly 20 bytes long, including the end of line.
    xref += QByteArray::number(m_offsets[id]).rightJustified(10, '0') + " 00000 n \n";
  }
  write(xref);

  const QByteArray fileId = QUuid::createUuid().toRfc4122().toHex();
  write("trailer\n<< /Size " + QByteArray::number(objectCount) + " /Root " + QByteArray::number(m_catalogId)
        + " 0 R /Info " + QByteArray::number(infoId) + " 0 R /ID [<" + fileId + "> <" + fileId + ">] >>\nstartxref\n"
        + QByteArray::number(xrefOffset) + "\n%%EOF\n");
  return m_ok;
}

QByteArray PdfWriter::formatNumber(const double value) {
  const double rounded = std::round(value * 1000.0) / 1000.0;
  QByteArray str = QByteArray::number(rounded, 'f', 3);
  while (str.endsWith('0')) {
    str.chop(1);
  }
  if (str.endsWith('.')) {
    str.chop(1);
  }
  if ((str == "-0") || str.isEmpty()) {
    str = "0";
  }
  return str;
}

QByteArray PdfWriter::textString(const QString& text) {
  QByteArray utf16 = "\xFE\xFF";
  for (const QChar ch : text) {
    const ushort code = ch.unicode();
    utf16 += static_cast<char>(code >> 8);
    utf16 += static_cast<char>(code & 0xFF);
  }
  return '<' + utf16.toHex().toUpper() + '>';
}

void PdfWriter::writeFont() {
  if (m_fontId != 0) {
    return;
  }
  m_fontId = allocateObject();
  const int cidFontId = allocateObject();
  const int cidToGidMapId = allocateObject();
  const int toUnicodeId = allocateObject();
  const int descriptorId = allocateObject();
  const int fontFileId = allocateObject();

  // The character codes are UTF-16 code units, used directly as CIDs.
  beginObject(m_fontId);
  write("<< /Type /Font /Subtype /Type0 /BaseFont /GlyphLessFont /Encoding /Identity-H /DescendantFonts [ "
        + QByteArray::number(cidFontId) + " 0 R ] /ToUnicode " + QByteArray::number(toUnicodeId) + " 0 R >>\nendobj\n");

  // Every glyph is half an em wide, which the text layer relies on.
  beginObject(cidFontId);
  write(
      "<< /Type /Font /Subtype /CIDFontType2 /BaseFont /GlyphLessFont /CIDSystemInfo << /Registry (Adobe) "
      "/Ordering (Identity) /Supplement 0 >> /FontDescriptor "
      + QByteArray::number(descriptorId) + " 0 R /DW 500 /CIDToGIDMap " + QByteArray::number(cidToGidMapId)
      + " 0 R >>\nendobj\n");

  // All CIDs map to glyph 1, the invisible glyph of the font.
  QByteArray cidToGidMap(2 * 65536, '\0');
  for (int i = 1; i < cidToGidMap.size(); i += 2) {
    cidToGidMap[i] = 1;
  }
  writeStreamObject(cidToGidMapId, "/Filter /FlateDecode", qCompress(cidToGidMap, 9).mid(4));

  // Maps the CIDs back to the same Unicode values, for searching and copying.
  const QByteArray toUnicode
      = "/CIDInit /ProcSet findresource begin\n12 dict begin\nbegincmap\n"
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def\n"
        "/CMapName /Adobe-Identity-UCS def\n/CMapType 2 def\n"
        "1 begincodespacerange\n<0000> <FFFF>\nendcodespacerange\n"
        "1 beginbfrange\n<0000> <FFFF> <0000>\nendbfrange\n"
        "endcmap\nCMapName currentdict /CMap defineresource pop\nend\nend\n";
  writeStreamObject(toUnicodeId, "/Filter /FlateDecode", qCompress(toUnicode, 9).mid(4));

  beginObject(descriptorId);
  write(
      "<< /Type /FontDescriptor /FontName /GlyphLessFont /Flags 5 /FontBBox [0 0 500 1000] /ItalicAngle 0 "
      "/Ascent 1000 /Descent -1 /CapHeight 1000 /StemV 80 /FontFile2 "
      + QByteArray::number(fontFileId) + " 0 R >>\nendobj\n");

  const QByteArray font = glyphLessFontData();
  writeStreamObject(fontFileId, "/Length1 " + QByteArray::number(font.size()) + " /Filter /FlateDecode",
                    qCompress(font, 9).mid(4));
}  // PdfWriter::writeFont

QByteArray PdfWriter::textContent(const Page& page) const {
  const double pageHeight = page.size.height();
  QByteArray text = "BT\n3 Tr\n";
  double currentFontSize = -1;
  for (const Word& word : page.words) {
    if (word.text.isEmpty() || (word.width <= 0) || (word.fontSize <= 0)) {
      continue;
    }
    QByteArray hex;
    hex.reserve(word.text.size() * 4);
    for (const QChar ch : word.text) {
      hex += QByteArray::number(ch.unicode(), 16).rightJustified(4, '0').toUpper();
    }
    if (word.fontSize != currentFontSize) {
      text += "/F0 " + formatNumber(word.fontSize) + " Tf\n";
      currentFontSize = word.fontSize;
    }
    // Stretch the glyphs, half an em each, to the width of the word.
    const double naturalWidth = static_cast<double>(word.text.size()) * word.fontSize * 0.5;
    text += formatNumber(100.0 * word.width / naturalWidth) + " Tz\n";
    text += "1 0 0 1 " + formatNumber(word.origin.x()) + ' ' + formatNumber(pageHeight - word.origin.y()) + " Tm\n<"
            + hex + "> Tj\n";
  }
  text += "ET\n";
  return text;
}

int PdfWriter::allocateObject() {
  m_offsets.push_back(-1);
  return static_cast<int>(m_offsets.size()) - 1;
}

void PdfWriter::beginObject(const int id) {
  m_offsets[id] = m_written;
  write(QByteArray::number(id) + " 0 obj\n");
}

void PdfWriter::writeStreamObject(const int id, const QByteArray& dictEntries, const QByteArray& data) {
  beginObject(id);
  write("<< " + dictEntries + " /Length " + QByteArray::number(data.size()) + " >>\nstream\n");
  write(data);
  write("\nendstream\nendobj\n");
}

void PdfWriter::write(const QByteArray& data) {
  if (!m_ok) {
    return;
  }
  if (m_device.write(data) != data.size()) {
    m_ok = false;
    return;
  }
  m_written += data.size();
}
