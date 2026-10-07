// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFWRITER_H_
#define SCANTAILOR_CORE_PDFWRITER_H_

#include <QByteArray>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <vector>

#include "NonCopyable.h"

class QIODevice;

/**
 * \brief A minimal PDF writer for image-only pages.
 *
 * Pages are written to the device as soon as they are added, so memory usage
 * doesn't grow with the number of pages.  The image data has to be encoded
 * already (CCITT G4, JBIG2 or JPEG), the writer only wraps it into PDF objects.
 *
 * The structure follows the approach of Tesseract's pdfrenderer.cpp: plain
 * objects, a classic cross-reference table and a trailer.  Recognized text is
 * written as invisible text (rendering mode 3) in Tesseract's embedded
 * "GlyphLessFont", stretched to the width of each word.  PDF/A additions (XMP
 * metadata, output intent) are meant to be added to the catalog later.
 */
class PdfWriter {
  DECLARE_NON_COPYABLE(PdfWriter)

 public:
  struct Image {
    enum class Encoding { CCITT_G4, JBIG2, JPEG, RAW, FLATE };

    Encoding encoding = Encoding::JPEG;
    /**
     * CCITT G4 data (0 = white, 1 = black, rows MSB first), JBIG2 segments
     * (see PdfImageEncoder::encodeJbig2), a complete JPEG file, or for RAW
     * the samples (see PdfImageEncoder::rawSamples) and for FLATE the same
     * compressed as a zlib stream (see PdfImageEncoder::deflate).
     */
    QByteArray data;
    int width = 0;
    int height = 0;
    /** JPEG, RAW and FLATE: 1 for grayscale, 3 for RGB.  Not used for palette images. */
    int components = 1;
    /** RAW and FLATE: 1, 2, 4 or 8.  G4 and JBIG2 images are always 1 bit gray, JPEG images 8 bit. */
    int bitsPerComponent = 8;
    /** RAW and FLATE: if not empty, the samples are indexes into these RGB triples. */
    QByteArray palette;
    /** FLATE only: the rows were filtered with PNG predictors before compressing. */
    bool pngPredictors = false;
    /**
     * 1 bit images only: paint the black pixels (samples 0) in black and leave
     * the rest of the page untouched.
     */
    bool isMask = false;
    /** Where the image goes on the page, in points, relative to the top left corner of the page. */
    QRectF rect;
  };

  /** A word of the invisible text layer (OCR). */
  struct Word {
    /** Including a trailing space if another word follows on the same line. */
    QString text;
    /** Left end of the word on the baseline, in points, relative to the top left corner of the page. */
    QPointF origin;
    /** The text is stretched to this width, in points. */
    double width = 0;
    /** In points.  The selection covers this height above the baseline. */
    double fontSize = 0;
  };

  struct Page {
    /** In points (1/72 inch). */
    QSizeF size;
    /** Painted in this order. */
    std::vector<Image> images;
    /** Written as invisible text on top of the images. */
    std::vector<Word> words;
  };

  /**
   * \param device Must be open for writing.  Only sequential writing is used.
   */
  explicit PdfWriter(QIODevice& device);

  /** Writes the file header.  Must be called first. */
  bool begin();

  /** Writes one page with its images. */
  bool addPage(const Page& page);

  /** Writes the page tree, the catalog, the cross-reference table and the trailer. */
  bool finish();

  int pageCount() const { return static_cast<int>(m_pageIds.size()); }

  /** Shown as the "Producer" in the document properties. */
  void setProducer(const QString& producer) { m_producer = producer; }

  /** Formats a number the way PDF expects it: no exponent, at most 3 decimals, no trailing zeros. */
  static QByteArray formatNumber(double value);

  /** Encodes a text string as UTF-16BE hex string with byte order mark, e.g. <FEFF0041>. */
  static QByteArray textString(const QString& text);

 private:
  /** Writes the font of the text layer, the first time it's needed. */
  void writeFont();

  QByteArray textContent(const Page& page) const;

  int allocateObject();

  void beginObject(int id);

  void writeStreamObject(int id, const QByteArray& dictEntries, const QByteArray& data);

  void write(const QByteArray& data);

  QIODevice& m_device;
  qint64 m_written = 0;
  /** Byte offset of each object; index 0 is the unused object 0. */
  std::vector<qint64> m_offsets;
  std::vector<int> m_pageIds;
  int m_catalogId = 0;
  int m_pagesId = 0;
  /** The Type0 font of the text layer; 0 until it's written. */
  int m_fontId = 0;
  QString m_producer;
  bool m_ok = true;
};


#endif  // SCANTAILOR_CORE_PDFWRITER_H_
