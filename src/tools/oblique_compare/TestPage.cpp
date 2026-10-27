// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "TestPage.h"

#include <GrayImage.h>
#include <Grayscale.h>
#include <Transform.h>

#include <QFontMetricsF>
#include <QPainter>
#include <QPolygonF>
#include <QTransform>
#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

using namespace imageproc;

namespace compare {

namespace {
const double DEG2RAD = M_PI / 180.0;

/** splitmix64: the same numbers on every machine and with every compiler. */
class Random {
 public:
  explicit Random(const uint64_t seed) : m_state(seed) {}

  uint64_t next() {
    uint64_t z = (m_state += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
  }

  int below(const int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }

  double uniform() { return static_cast<double>(next() >> 11) * (1.0 / 9007199254740992.0); }

  double uniform(const double from, const double to) { return from + (to - from) * uniform(); }

  double gaussian() {
    const double u1 = std::max(uniform(), 1e-12);
    const double u2 = uniform();
    return std::sqrt(-2.0 * std::log(u1)) * std::cos(2.0 * M_PI * u2);
  }

 private:
  uint64_t m_state;
};

uint64_t pageSeed(const uint32_t seed, const int index, const uint32_t purpose) {
  return (static_cast<uint64_t>(seed) << 32) ^ (static_cast<uint64_t>(purpose) << 24) ^ static_cast<uint64_t>(index);
}

double mm(const double millimeters, const int dpi) {
  return millimeters * dpi / 25.4;
}

const char16_t* const WORDS[] = {
    u"die",    u"mit",      u"ist",        u"nicht",      u"Licht",   u"Bild",     u"wirklich", u"Linie",    u"Seite",
    u"bis",    u"ihr",      u"sich",       u"Zeit",       u"Kirche",  u"Richtung", u"Inhalt",   u"Initiale", u"in",
    u"im",     u"wie",      u"vielleicht", u"Finsternis", u"Gericht", u"Kristall", u"Mitte",    u"Pflicht",  u"Fischer",
    u"Wissen", u"Schrift",  u"Papier",     u"Titel",      u"Kapitel", u"und",      u"der",      u"das",      u"ein",
    u"zu",     u"auf",      u"Hand",       u"Stadt",      u"Wasser",  u"Tal",      u"Weg",      u"Vater",    u"Yacht",
    u"Fluss",  u"Park",     u"Ärger",      u"Öffnung",    u"Übung",   u"Äpfel",    u"Öl",       u"Überall",  u"für",
    u"über",   u"können",   u"müssen",     u"Größe",      u"Bücher",  u"Mädchen",  u"Tür",      u"hören",    u"würde",
    u"später", u"Frühling", u"möglich",    u"nämlich",    u"Dr.",     u"Nr.",      u"Tel."};
const int NUM_WORDS = sizeof(WORDS) / sizeof(WORDS[0]);

QString word(Random& rnd) {
  QString result(QString::fromUtf16(WORDS[rnd.below(NUM_WORDS)]));
  if (result.endsWith(QLatin1Char('.'))) {
    return result;
  }
  const double r = rnd.uniform();
  if (r < 0.12) {
    result += QLatin1Char('.');
  } else if (r < 0.24) {
    result += QLatin1Char(',');
  }
  return result;
}

/** A dark area with lighter shapes, like a photo after binarization. */
void drawPicture(QPainter& painter, const QRectF& rect, Random& rnd) {
  painter.fillRect(rect, QColor(60, 60, 60));
  for (int i = 0; i < 6; ++i) {
    const int gray = 100 + rnd.below(136);
    const double w = rnd.uniform(0.15, 0.5) * rect.width();
    const double h = rnd.uniform(0.15, 0.5) * rect.height();
    const QPointF center(rect.left() + rnd.uniform(w / 2, rect.width() - w / 2),
                         rect.top() + rnd.uniform(h / 2, rect.height() - h / 2));
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(gray, gray, gray));
    painter.drawEllipse(center, w / 2, h / 2);
  }
}

struct Column {
  double left;
  double right;
};

/** imageproc works on 8-bit images with a gray palette, not on Format_Grayscale8. */
QImage grayCanvas(const QSize& size) {
  QImage image(size, QImage::Format_Indexed8);
  image.setColorTable(createGrayscalePalette());
  return image;
}

QImage blurred(const QImage& src, const double sigma) {
  const int radius = std::max(1, static_cast<int>(std::ceil(2.5 * sigma)));
  std::vector<int> kernel(2 * radius + 1);
  int sum = 0;
  for (int i = -radius; i <= radius; ++i) {
    kernel[i + radius] = static_cast<int>(std::lround(1024.0 * std::exp(-0.5 * i * i / (sigma * sigma))));
    sum += kernel[i + radius];
  }
  const int w = src.width();
  const int h = src.height();
  QImage tmp(grayCanvas(src.size()));
  for (int y = 0; y < h; ++y) {
    const uint8_t* in = src.constScanLine(y);
    uint8_t* out = tmp.scanLine(y);
    for (int x = 0; x < w; ++x) {
      int acc = 0;
      for (int k = -radius; k <= radius; ++k) {
        acc += kernel[k + radius] * in[std::clamp(x + k, 0, w - 1)];
      }
      out[x] = static_cast<uint8_t>((acc + sum / 2) / sum);
    }
  }
  QImage dst(grayCanvas(src.size()));
  for (int y = 0; y < h; ++y) {
    uint8_t* out = dst.scanLine(y);
    for (int x = 0; x < w; ++x) {
      int acc = 0;
      for (int k = -radius; k <= radius; ++k) {
        acc += kernel[k + radius] * tmp.constScanLine(std::clamp(y + k, 0, h - 1))[x];
      }
      out[x] = static_cast<uint8_t>((acc + sum / 2) / sum);
    }
  }
  return dst;
}

QImage disturbed(const QImage& src, const PageSpec& spec, Random rnd) {
  switch (spec.disturbance) {
    case Disturbance::NONE:
      return src;
    case Disturbance::BLUR:
      return blurred(src, mm(0.07, spec.dpi));
    case Disturbance::NOISE: {
      QImage dst(src.copy());
      for (int y = 0; y < dst.height(); ++y) {
        uint8_t* line = dst.scanLine(y);
        for (int x = 0; x < dst.width(); ++x) {
          line[x] = static_cast<uint8_t>(
              std::clamp(static_cast<int>(std::lround(line[x] + 14.0 * rnd.gaussian())), 0, 255));
        }
      }
      return dst;
    }
  }
  return src;
}

/** The page in perspective: the top narrower and the bottom wider by half of \p keystone each. */
QImage withKeystone(const QImage& page, const double keystone) {
  const double w = page.width();
  const double h = page.height();
  const double d = 0.25 * keystone * w;
  QPolygonF rect;
  rect << QPointF(0, 0) << QPointF(w, 0) << QPointF(w, h) << QPointF(0, h);
  QPolygonF trapezoid;
  trapezoid << QPointF(d, 0) << QPointF(w - d, 0) << QPointF(w + d, h) << QPointF(-d, h);
  QTransform xform;
  QTransform::quadToQuad(rect, trapezoid, xform);
  QImage result(page.size(), QImage::Format_RGB32);
  result.fill(Qt::white);
  QPainter painter(&result);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.setTransform(xform);
  painter.drawImage(0, 0, page);
  return result;
}

/** Where the words come from, and which of them are set in italics. */
class WordSource {
 public:
  WordSource(Random& rnd, Random& italicRnd, const Italic italic)
      : m_rnd(rnd), m_italicRnd(italicRnd), m_italic(italic) {}

  /** The next word; \p italic tells whether it is set in italics. */
  QString next(bool& italic) {
    switch (m_italic) {
      case Italic::PAGE:
        italic = true;
        break;
      case Italic::WORDS:
        // Phrases of a few words, as for titles or emphasis.
        m_inPhrase = m_italicRnd.uniform() < (m_inPhrase ? 0.7 : 0.06);
        italic = m_inPhrase;
        break;
      default:
        italic = false;
        break;
    }
    return word(m_rnd);
  }

 private:
  Random& m_rnd;
  Random& m_italicRnd;
  Italic m_italic;
  bool m_inPhrase = false;
};

/** Sets text in one size, upright or in italics. */
class LineSetter {
 public:
  LineSetter(const QFont& font, const double pixelSize)
      : m_font(font), m_italicFont(font), m_metrics(m_font), m_italicMetrics(m_italicFont) {
    const int px = std::max(1, static_cast<int>(std::lround(pixelSize)));
    m_font.setPixelSize(px);
    m_italicFont.setPixelSize(px);
    m_italicFont.setItalic(true);
    m_metrics = QFontMetricsF(m_font);
    m_italicMetrics = QFontMetricsF(m_italicFont);
  }

  double space() const { return m_metrics.horizontalAdvance(QLatin1Char(' ')); }

  double ascent() const { return m_metrics.ascent(); }

  double descent() const { return m_metrics.descent(); }

  double pixelSize() const { return m_font.pixelSize(); }

  double width(const QString& text, const bool italic = false) const {
    return (italic ? m_italicMetrics : m_metrics).horizontalAdvance(text);
  }

  void draw(QPainter& painter,
            const QString& text,
            const double x,
            const double baseline,
            const bool italic = false) const {
    painter.setFont(italic ? m_italicFont : m_font);
    painter.drawText(QPointF(x, baseline), text);
  }

 private:
  QFont m_font;
  QFont m_italicFont;
  QFontMetricsF m_metrics;
  QFontMetricsF m_italicMetrics;
};

/** How paragraphs start: indented first lines, list items with a bullet and a hanging indent. */
struct ParagraphStyle {
  bool indents = false;
  bool lists = false;
  double indent = 0;
  double listIndent = 0;
};

/** Where the text of a column stands: at the start of a paragraph, inside a list item. */
struct Paragraph {
  bool starts = true;
  bool listItem = false;
};

struct Word {
  QString text;
  double width;
  bool italic;
};

/**
 * One line between \p left and \p right. Justified lines fill the column except at the end of a
 * paragraph; ragged lines stop after the last word that fits; centered lines are of random length.
 * \p layoutRnd decides on list items, so that pages without lists stay as they were before.
 */
void setLine(QPainter& painter,
             const LineSetter& setter,
             const Alignment alignment,
             const ParagraphStyle& style,
             const double left,
             const double right,
             const double baseline,
             Random& rnd,
             WordSource& words,
             Random& layoutRnd,
             Paragraph& paragraph) {
  const bool paragraphEnd = rnd.uniform() < 0.1;
  if (paragraph.starts) {
    paragraph.listItem = style.lists && (layoutRnd.below(4) == 0);
  }
  double textLeft = left;
  if (paragraph.listItem) {
    textLeft = left + style.listIndent;
    if (paragraph.starts) {
      setter.draw(painter, QString(QChar(0x2022)), left, baseline);
    }
  } else if (paragraph.starts && style.indents && (alignment != Alignment::CENTERED)) {
    textLeft = left + style.indent;
  }
  paragraph.starts = paragraphEnd;

  double lineEnd = right;
  if (alignment == Alignment::CENTERED) {
    lineEnd = textLeft + rnd.uniform(0.35, 1.0) * (right - textLeft);
  } else if (paragraphEnd) {
    lineEnd = textLeft + rnd.uniform(0.3, 0.8) * (right - textLeft);
  }

  std::vector<Word> line;
  double used = 0;
  while (true) {
    bool italic = false;
    const QString w(words.next(italic));
    const double width = setter.width(w, italic);
    const double needed = (line.empty() ? 0.0 : setter.space()) + width;
    if (textLeft + used + needed > lineEnd) {
      break;
    }
    used += needed;
    line.push_back({w, width, italic});
  }

  double x = textLeft;
  double gap = setter.space();
  if ((alignment == Alignment::JUSTIFIED) && !paragraphEnd && (line.size() > 1)) {
    gap += (lineEnd - textLeft - used) / static_cast<double>(line.size() - 1);
  } else if (alignment == Alignment::CENTERED) {
    x += 0.5 * (right - textLeft - used);
  }
  for (const Word& w : line) {
    setter.draw(painter, w.text, x, baseline, w.italic);
    x += w.width + gap;
  }
}
}  // namespace

QString toString(const TextAmount text) {
  switch (text) {
    case TextAmount::FULL_PAGE:
      return QStringLiteral("full page");
    case TextAmount::HALF_PAGE:
      return QStringLiteral("half page");
    default:
      return QStringLiteral("few lines");
  }
}

QString toString(const Layout layout) {
  switch (layout) {
    case Layout::ONE_COLUMN:
      return QStringLiteral("one column");
    case Layout::TWO_COLUMNS:
      return QStringLiteral("two columns");
    default:
      return QStringLiteral("text with picture");
  }
}

QString toString(const Alignment alignment) {
  switch (alignment) {
    case Alignment::JUSTIFIED:
      return QStringLiteral("justified");
    case Alignment::RAGGED_RIGHT:
      return QStringLiteral("ragged right");
    default:
      return QStringLiteral("centered");
  }
}

QString toString(const Disturbance disturbance) {
  switch (disturbance) {
    case Disturbance::NONE:
      return QStringLiteral("none");
    case Disturbance::NOISE:
      return QStringLiteral("noise");
    default:
      return QStringLiteral("blur");
  }
}

QString toString(const ShearClass shearClass) {
  switch (shearClass) {
    case ShearClass::NONE:
      return QStringLiteral("0°");
    case ShearClass::SMALL:
      return QStringLiteral("0.1° to 1°");
    default:
      return QStringLiteral("1° to 4°");
  }
}

QString toString(const Italic italic) {
  switch (italic) {
    case Italic::NONE:
      return QStringLiteral("no italics");
    case Italic::WORDS:
      return QStringLiteral("italic words");
    default:
      return QStringLiteral("all italic");
  }
}

PageSpec pageSpec(const int index, const uint32_t seed) {
  Random rnd(pageSeed(seed, index, 1));
  PageSpec spec;
  spec.index = index;
  spec.text = static_cast<TextAmount>(index % 3);
  spec.layout = static_cast<Layout>((index / 3) % 3);
  spec.alignment = static_cast<Alignment>((index / 9) % 3);
  static const int DPIS[] = {150, 300, 600};
  spec.dpi = DPIS[(index / 27) % 3];
  spec.disturbance = static_cast<Disturbance>(rnd.below(3));
  spec.rotationDeg = rnd.uniform(-5.0, 5.0);
  const int shearDraw = rnd.below(8);
  const double sign = (rnd.below(2) == 0) ? -1.0 : 1.0;
  if (shearDraw <= 1) {
    spec.shearClass = ShearClass::NONE;
    if (shearDraw == 1) {
      Random keystoneRnd(pageSeed(seed, index, 5));
      spec.keystone = ((keystoneRnd.below(2) == 0) ? -1.0 : 1.0) * keystoneRnd.uniform(0.01, 0.06);
    }
  } else if (shearDraw <= 3) {
    spec.shearClass = ShearClass::SMALL;
    spec.shearDeg = sign * rnd.uniform(0.1, 1.0);
  } else {
    spec.shearClass = ShearClass::LARGE;
    spec.shearDeg = sign * rnd.uniform(1.0, 4.0);
  }

  Random extras(pageSeed(seed, index, 6));
  spec.serif = (extras.below(2) == 0);
  const int italicDraw = extras.below(6);
  spec.italic = (italicDraw == 0) ? Italic::PAGE : (italicDraw == 1) ? Italic::WORDS : Italic::NONE;
  spec.headerFooter = (extras.below(2) == 0);
  spec.indents = (extras.below(2) == 0);
  spec.lists = (extras.below(4) == 0);
  return spec;
}

QFont textFont(const bool serif) {
  QFont font(serif ? QStringLiteral("Times New Roman") : QStringLiteral("Arial"));
  font.setStyleHint(serif ? QFont::Serif : QFont::SansSerif);
  return font;
}

QImage renderPage(const PageSpec& spec, const uint32_t seed) {
  Random rnd(pageSeed(seed, spec.index, 2));
  Random italicRnd(pageSeed(seed, spec.index, 7));
  Random layoutRnd(pageSeed(seed, spec.index, 8));
  WordSource words(rnd, italicRnd, spec.italic);
  const QFont font(textFont(spec.serif));
  const int dpi = spec.dpi;
  const QSize pageSize(static_cast<int>(std::lround(mm(210, dpi))), static_cast<int>(std::lround(mm(297, dpi))));

  QImage canvas(pageSize, QImage::Format_RGB32);
  canvas.fill(Qt::white);
  QPainter painter(&canvas);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::TextAntialiasing);
  painter.setPen(Qt::black);

  const double left = mm(20, dpi);
  const double right = pageSize.width() - mm(20, dpi);
  const double top = mm(25, dpi);
  const double bottom = pageSize.height() - mm(25, dpi);

  const LineSetter body(font, 10.5 / 72.0 * dpi);
  const LineSetter headline(font, 19.0 / 72.0 * dpi);
  const double lineHeight = 1.4 * body.pixelSize();

  if (spec.headerFooter) {
    // A running head with the chapter left and the page number right, and a page number below.
    const LineSetter small(font, 9.0 / 72.0 * dpi);
    const QString chapter(QStringLiteral("Kapitel %1 · %2").arg(1 + spec.index % 12).arg(word(layoutRnd)));
    const QString number(QString::number(1 + spec.index));
    const double headBaseline = top - mm(10, dpi);
    small.draw(painter, chapter, left, headBaseline, true);
    small.draw(painter, number, right - small.width(number), headBaseline);
    small.draw(painter, number, 0.5 * (left + right - small.width(number)), bottom + mm(12, dpi));
  }

  ParagraphStyle style;
  style.indents = spec.indents;
  style.lists = spec.lists;
  style.indent = body.pixelSize();
  style.listIndent = mm(4, dpi);

  std::vector<Column> columns;
  if (spec.layout == Layout::TWO_COLUMNS) {
    const double middle = 0.5 * (left + right);
    columns.push_back({left, middle - mm(4, dpi)});
    columns.push_back({middle + mm(4, dpi), right});
  } else {
    columns.push_back({left, right});
  }

  double textTop = top;
  int lineLimit = 1 << 30;
  double textBottom = bottom;
  if (spec.text == TextAmount::FEW_LINES) {
    // A chapter start: a headline and a few lines below it.
    const double baseline = top + headline.ascent();
    QString title;
    for (int i = 0; i < 3; ++i) {
      title += (i > 0 ? QStringLiteral(" ") : QString()) + QString::fromUtf16(WORDS[rnd.below(NUM_WORDS)]);
    }
    const double titleLeft
        = (spec.alignment == Alignment::CENTERED) ? 0.5 * (left + right - headline.width(title)) : left;
    headline.draw(painter, title, titleLeft, baseline);
    textTop = baseline + headline.descent() + lineHeight;
    lineLimit = 3;
  } else if (spec.text == TextAmount::HALF_PAGE) {
    textBottom = 0.5 * (top + bottom);
  }

  QRectF picture;
  if (spec.layout == Layout::TEXT_WITH_PICTURE) {
    picture = QRectF(left, textTop, 0.45 * (right - left), mm(55, dpi));
    drawPicture(painter, picture, rnd);
    painter.setPen(Qt::black);
  }

  int lines = 0;
  for (const Column& column : columns) {
    Paragraph paragraph;
    for (double baseline = textTop + body.ascent(); baseline + body.descent() <= textBottom && lines < lineLimit;
         baseline += lineHeight) {
      double lineLeft = column.left;
      if (!picture.isNull() && (baseline + body.descent() > picture.top())
          && (baseline - body.ascent() < picture.bottom() + mm(3, dpi))) {
        lineLeft = picture.right() + mm(5, dpi);
      }
      setLine(painter, body, spec.alignment, style, lineLeft, column.right, baseline, rnd, words, layoutRnd, paragraph);
      ++lines;
    }
  }
  painter.end();
  if (spec.keystone != 0) {
    canvas = withKeystone(canvas, spec.keystone);
  }
  return GrayImage(canvas).toQImage();
}

QImage scannedPage(const QImage& upright, const PageSpec& spec, const uint32_t seed, const QPointF& shift) {
  const QPointF center(0.5 * upright.width(), 0.5 * upright.height());
  const QTransform shear(1, 0, std::tan(spec.shearDeg * DEG2RAD), 1, 0, 0);
  QTransform rotation;
  rotation.rotate(-spec.rotationDeg);
  const QTransform xform(QTransform::fromTranslate(-center.x(), -center.y()) * shear * rotation
                         * QTransform::fromTranslate(center.x(), center.y()));
  // The page area stays the same for every shift, so that only the sampling grid moves.
  const QRect dstRect(xform.mapRect(QRectF(upright.rect())).toAlignedRect().adjusted(-1, -1, 1, 1));
  const QTransform shifted(xform * QTransform::fromTranslate(shift.x(), shift.y()));
  const GrayImage scan(transformToGray(upright, shifted, dstRect, OutsidePixels::assumeColor(Qt::white)));
  return disturbed(scan.toQImage(), spec, Random(pageSeed(seed, spec.index, 4)));
}

}  // namespace compare
