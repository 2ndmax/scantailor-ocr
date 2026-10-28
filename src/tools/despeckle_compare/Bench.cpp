// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Bench.h"

#include <BinaryImage.h>
#include <ConnectivityMap.h>
#include <Despeckle.h>
#include <Dpi.h>
#include <NullTaskStatus.h>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <random>
#include <vector>

#include "SpotSheet.h"

using namespace imageproc;

namespace {
const double kPi = 3.14159265358979323846;

enum TextClass : uint8_t { kNoText, kLetter, kDotAbove, kPunctuation, kLeader, kSuperscript, kTextClasses };
const char* const kTextClassNames[kTextClasses]
    = {"", "letter", "dot above", "punctuation", "leader dot", "superscript"};

enum NoiseClass : uint8_t { kNoNoise, kSalt, kDust, kFibre, kBorder, kNearText, kShowThrough, kNoiseClasses };
const char* const kNoiseClassNames[kNoiseClasses]
    = {"", "salt", "dust", "fibre", "border", "near text", "show-through"};

// A class code per pixel, 0 = white.
struct Layer {
  Layer(int w, int h) : width(w), height(h), cls(size_t(w) * h, 0) {}

  bool contains(int x, int y) const { return (x >= 0) && (y >= 0) && (x < width) && (y < height); }

  uint8_t& at(int x, int y) { return cls[size_t(y) * width + x]; }

  uint8_t at(int x, int y) const { return cls[size_t(y) * width + x]; }

  int width;
  int height;
  std::vector<uint8_t> cls;
  // Optional: per pixel, the part of a glyph drawn there (a connected component of the glyph as
  // rendered, e.g. the dot of an i), numbered over the page from 1; 0 = none.
  std::vector<uint32_t> part;
  uint32_t nextPart = 1;
};

enum class RunKind { Normal, Leader, Superscript };

struct GlyphPixel {
  int x;  // relative to the pen position on the baseline
  int y;
  uint8_t cls;
  uint8_t ink;   // coverage by the glyph, 0-255
  uint8_t part;  // index of the glyph's component
};

struct Glyph {
  int advance = 0;
  int parts = 0;
  std::vector<GlyphPixel> pixels;
};

uint8_t classify(const QChar c, const RunKind kind, const bool largestComponent) {
  if (kind == RunKind::Leader) {
    return kLeader;
  }
  if (kind == RunKind::Superscript) {
    return kSuperscript;
  }
  // Punctuation, ellipsis, hyphen, en dash and German quotes
  if (QStringView(u".,;:!?…-–„“").contains(c)) {
    return kPunctuation;
  }
  // All but the body of these letters are dots above it.
  if (!largestComponent && QStringView(u"ijäöüÄÖÜ").contains(c)) {
    return kDotAbove;
  }
  return kLetter;
}

// Renders single characters with antialiasing, split into classified components. Keeps the pixels
// that may turn black when thresholded with noise (see kMinInk).
class GlyphCache {
 public:
  explicit GlyphCache(const QFont& font) : m_font(font), m_metrics(font) {}

  const QFontMetrics& metrics() const { return m_metrics; }

  const Glyph& glyph(const QChar c, const RunKind kind) {
    const std::pair<char16_t, int> key(c.unicode(), int(kind));
    auto it = m_cache.find(key);
    if (it == m_cache.end()) {
      it = m_cache.emplace(key, render(c, kind)).first;
    }
    return it->second;
  }

 private:
  Glyph render(QChar c, RunKind kind) const;

  QFont m_font;
  QFontMetrics m_metrics;
  std::map<std::pair<char16_t, int>, Glyph> m_cache;
};

// The lowest ink that the noisy threshold may turn black: 128 - kPageShift - kPixelNoise.
const int kPageShift = 20;
const int kPixelNoise = 40;
const int kMinInk = 128 - kPageShift - kPixelNoise;

int inkAt(const QImage& image, const int x, const int y) {
  return 255 - qGray(image.pixel(x, y));
}

Glyph GlyphCache::render(const QChar c, const RunKind kind) const {
  Glyph glyph;
  glyph.advance = m_metrics.horizontalAdvance(c);
  const int pad = m_metrics.height();
  const QPoint origin(pad, 2 * pad);
  QImage image(glyph.advance + 2 * pad, 3 * pad, QImage::Format_RGB32);
  image.fill(Qt::white);
  {
    QPainter painter(&image);
    painter.setFont(m_font);
    painter.setPen(Qt::black);
    painter.drawText(origin, QString(c));
  }

  const int w = image.width();
  const int h = image.height();
  std::vector<int> label(size_t(w) * h, -1);
  std::vector<std::vector<QPoint>> components;
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      if ((inkAt(image, x, y) < kMinInk) || (label[size_t(y) * w + x] >= 0)) {
        continue;
      }
      const int id = int(components.size());
      components.emplace_back();
      std::vector<QPoint> stack{{x, y}};
      label[size_t(y) * w + x] = id;
      while (!stack.empty()) {
        const QPoint p = stack.back();
        stack.pop_back();
        components[id].push_back(p);
        for (int dy = -1; dy <= 1; ++dy) {
          for (int dx = -1; dx <= 1; ++dx) {
            const int nx = p.x() + dx;
            const int ny = p.y() + dy;
            if ((nx < 0) || (ny < 0) || (nx >= w) || (ny >= h) || (label[size_t(ny) * w + nx] >= 0)
                || (inkAt(image, nx, ny) < kMinInk)) {
              continue;
            }
            label[size_t(ny) * w + nx] = id;
            stack.emplace_back(nx, ny);
          }
        }
      }
    }
  }

  glyph.parts = int(components.size());
  size_t largest = 0;
  for (size_t i = 0; i < components.size(); ++i) {
    if (components[i].size() > components[largest].size()) {
      largest = i;
    }
  }
  for (size_t i = 0; i < components.size(); ++i) {
    const uint8_t cls = classify(c, kind, i == largest);
    for (const QPoint& p : components[i]) {
      glyph.pixels.push_back(
          {p.x() - origin.x(), p.y() - origin.y(), cls, uint8_t(inkAt(image, p.x(), p.y())), uint8_t(i)});
    }
  }
  return glyph;
}

struct Run {
  QString text;
  RunKind kind;
};

using Line = std::vector<Run>;

// Lays out and draws lines of text into a layer of text classes.
class Typesetter {
 public:
  explicit Typesetter(const QFont& font)
      : m_normal(font), m_small(smallerFont(font)), m_raise(int(std::lround(font.pixelSize() * 0.35))) {}

  int lineSpacing() const { return m_normal.metrics().lineSpacing(); }

  int ascent() const { return m_normal.metrics().ascent(); }

  int width(const Line& line) {
    int width = 0;
    for (const Run& run : line) {
      for (const QChar c : run.text) {
        width += cache(run.kind).glyph(c, run.kind).advance;
      }
    }
    return width;
  }

  // Draws the pixels for which inked(ink) says they turn black.
  void draw(const Line& line, int x, const int baseline, Layer& layer, const std::function<bool(int)>& inked) {
    for (const Run& run : line) {
      const int y = baseline - ((run.kind == RunKind::Superscript) ? m_raise : 0);
      for (const QChar c : run.text) {
        const Glyph& glyph = cache(run.kind).glyph(c, run.kind);
        for (const GlyphPixel& p : glyph.pixels) {
          if (layer.contains(x + p.x, y + p.y) && inked(p.ink)) {
            layer.at(x + p.x, y + p.y) = p.cls;
            if (!layer.part.empty()) {
              layer.part[size_t(y + p.y) * layer.width + x + p.x] = layer.nextPart + p.part;
            }
          }
        }
        x += glyph.advance;
        layer.nextPart += glyph.parts;
      }
    }
  }

  // Greedy line breaking. "^12" in the text is a superscript 12.
  std::vector<Line> wrap(const QString& text, const int maxWidth) {
    std::vector<Line> lines(1);
    for (const QString& word : text.split(u' ', Qt::SkipEmptyParts)) {
      Line runs;
      for (const QString& part : word.split(u'^')) {
        if (part.isEmpty()) {
          continue;
        }
        const bool superscript = !runs.empty() || word.startsWith(u'^');
        int digits = 0;
        while (superscript && (digits < part.size()) && part[digits].isDigit()) {
          ++digits;
        }
        if (digits > 0) {
          runs.push_back({part.left(digits), RunKind::Superscript});
        }
        if (digits < part.size()) {
          runs.push_back({part.mid(digits), RunKind::Normal});
        }
      }
      Line candidate = lines.back();
      if (!candidate.empty()) {
        candidate.push_back({QStringLiteral(" "), RunKind::Normal});
      }
      candidate.insert(candidate.end(), runs.begin(), runs.end());
      if (lines.back().empty() || (width(candidate) <= maxWidth)) {
        lines.back() = candidate;
      } else {
        lines.push_back(runs);
      }
    }
    return lines;
  }

  // A table of contents line: the title, a dot leader up to the page number at the right edge.
  // Returns the part on the left; 'pageWidth' is the width reserved for the page number.
  Line tocLine(const QString& title, const int pageWidth, const int maxWidth, const bool spacedLeader) {
    Line line{{title + QStringLiteral(" "), RunKind::Normal}};
    const QString unit = spacedLeader ? QStringLiteral(". ") : QStringLiteral(".");
    const int unitWidth = width({{unit, RunKind::Leader}});
    const int space = width({{QStringLiteral("  "), RunKind::Normal}});
    const int count = (maxWidth - width(line) - pageWidth - space) / std::max(1, unitWidth);
    if (count > 0) {
      line.push_back({unit.repeated(count), RunKind::Leader});
    }
    return line;
  }

 private:
  static QFont smallerFont(const QFont& font) {
    QFont small(font);
    small.setPixelSize(std::max(1, int(std::lround(font.pixelSize() * 0.6))));
    return small;
  }

  GlyphCache& cache(const RunKind kind) { return (kind == RunKind::Superscript) ? m_small : m_normal; }

  GlyphCache m_normal;
  GlyphCache m_small;
  int m_raise;
};

// The text of the pages, with German umlauts, sharp s, quotes and dashes.
const char16_t* const kParagraphs[] = {
    u"Die Überprüfung der Seiten ergab, dass 3,75 % aller Abbildungen fehlerhaft waren^1. "
    u"Größere Mängel – etwa verschmierte Ränder oder fehlende Zeilen – traten "
    u"jedoch nur selten auf; in 12 von 1.250 Fällen (d. h. ungefähr 0,96 %) musste neu gescannt "
    u"werden. Warum? Weil Staub, Fasern und Knicke im Papier häufig übersehen werden!",
    u"Für jede Seite wird zunächst geprüft, ob Text, Tabellen oder Bilder vorliegen. "
    u"Anschließend folgen Ausrichtung, Zuschnitt und Entzerrung^2. Jüngere Verfahren nutzen "
    u"zusätzlich die Lage der Zeilen, z. B. bei gewölbten Buchseiten; ältere dagegen nur die "
    u"Ränder … Ob das genügt, hängt vom Einzelfall ab: „jein“, sagt der Fachmann.",
    u"Zwölf Boxkämpfer jagen Viktor quer über den großen Sylter Deich. Im Jahr 1879 "
    u"erschienen bereits 4.316 Exemplare; die Auflage stieg bis 1902 auf 17.040^3. Siehe S. 45 ff. sowie "
    u"Anm. 7; vgl. ferner Kap. 2.3, Abs. iii, und die Übersicht in Tab. 4.",
};

struct TocEntry {
  const char16_t* title;
  const char* page;
};

const TocEntry kToc[] = {
    {u"1  Einleitung", "1"},
    {u"1.1  Ziel der Untersuchung", "3"},
    {u"1.2  Überblick über die Verfahren", "7"},
    {u"2  Grundlagen der Bildverarbeitung", "15"},
    {u"2.1  Schwellwerte und Binärbilder", "18"},
    {u"2.2  Rauschen, Staub und Flecken", "24"},
    {u"3  Ergebnisse", "31"},
    {u"Anhang: Tabellen, Quellen, Register", "102"},
};

const char16_t* const kFootnotes[] = {
    u"^1 Gezählt wurden nur Seiten mit Text.",
    u"^2 Vgl. dazu Abschnitt 1.2, S. 7–9.",
    u"^3 Angaben des Verlags; sie sind nicht überprüft.",
};

struct PageText {
  Line left;
  Line right;  // drawn right-aligned, e.g. a page number in the table of contents
};

// The text of a page from top to bottom, repeated as needed to fill it. Empty lines separate blocks.
std::vector<PageText> composeText(Typesetter& typesetter, const int maxWidth, const int maxLines) {
  std::vector<PageText> lines;
  bool spacedLeader = true;
  while (int(lines.size()) < maxLines) {
    for (const char16_t* paragraph : kParagraphs) {
      for (Line& line : typesetter.wrap(QString::fromUtf16(paragraph), maxWidth)) {
        lines.push_back({std::move(line), {}});
      }
      lines.emplace_back();
      if (paragraph == kParagraphs[0]) {
        for (const TocEntry& entry : kToc) {
          const Line page{{QString::fromLatin1(entry.page), RunKind::Normal}};
          const int pageWidth = typesetter.width(page);
          lines.push_back(
              {typesetter.tocLine(QString::fromUtf16(entry.title), pageWidth, maxWidth, spacedLeader), page});
        }
        spacedLeader = !spacedLeader;
        lines.emplace_back();
      }
    }
    for (const char16_t* footnote : kFootnotes) {
      for (Line& line : typesetter.wrap(QString::fromUtf16(footnote), maxWidth)) {
        lines.push_back({std::move(line), {}});
      }
    }
    lines.emplace_back();
  }
  return lines;
}

// Adds noise of the various kinds to a page, never touching its text, so that every component of
// the page is either text or noise.
class NoiseMaker {
 public:
  NoiseMaker(const Layer& text, Layer& noise, const uint32_t seed, const double scale)
      : m_text(text), m_noise(noise), m_rng(seed), m_scale(scale) {}

  // Single pixels and tiny clusters of 1-4 pixels, independent of the resolution.
  void salt(const int count) {
    for (int i = 0; i < count; ++i) {
      blob(randomPoint(), uniform(1, 4), kSalt);
    }
  }

  // Clumps of 5-60 pixels at 300 dpi.
  void dust(const int count) {
    for (int i = 0; i < count; ++i) {
      blob(randomPoint(), scaledArea(uniform(5, 60)), kDust);
    }
  }

  // Thin, slightly curved strokes of 0.4-2.5 mm.
  void fibres(const int count, const int dpi) {
    const int thickness = std::max(1, int(std::lround(m_scale)));
    for (int i = 0; i < count; ++i) {
      const QPoint start = randomPoint();
      double x = start.x();
      double y = start.y();
      double angle = uniformReal(0, 2 * kPi);
      const double curve = uniformReal(-0.03, 0.03) / m_scale;
      const int length = int(uniformReal(0.4, 2.5) * dpi / 25.4);
      for (int step = 0; step < length; ++step) {
        for (int dy = 0; dy < thickness; ++dy) {
          for (int dx = 0; dx < thickness; ++dx) {
            put(int(x) + dx, int(y) + dy, kFibre);
          }
        }
        x += std::cos(angle);
        y += std::sin(angle);
        angle += curve;
      }
    }
  }

  // Dense clumps in a band along the left and right edges of the page.
  void border(const int bandWidth, const int count) {
    for (int i = 0; i < count; ++i) {
      const int x = uniform(0, bandWidth - 1);
      const QPoint p(uniform(0, 1) ? x : m_noise.width - 1 - x, uniform(0, m_noise.height - 1));
      blob(p, scaledArea(uniform(1, 40)), kBorder);
    }
  }

  // Clumps of 1-6 pixels (at 300 dpi) 2-4 pixels away from a letter, where they look like a mark.
  void nearText(const int count) {
    int placed = 0;
    for (int tries = 0; (placed < count) && (tries < count * 1000); ++tries) {
      const QPoint p = randomPoint();
      if (m_text.at(p.x(), p.y()) == kNoText) {
        continue;
      }
      const double angle = uniformReal(0, 2 * kPi);
      const double distance = uniformReal(2, 4) * m_scale;
      const QPoint q(p.x() + int(std::lround(std::cos(angle) * distance)),
                     p.y() + int(std::lround(std::sin(angle) * distance)));
      if (canPlace(q.x(), q.y())) {
        blob(q, scaledArea(uniform(1, 6)), kNearText);
        ++placed;
      }
    }
  }

  // Fragments of mirrored text from the back of the sheet: the parts of 'back' where a smooth
  // random field is high.
  void showThrough(const Layer& back, const int cell) {
    const int cols = m_noise.width / cell + 2;
    const int rows = m_noise.height / cell + 2;
    std::vector<double> field(size_t(cols) * rows);
    for (double& value : field) {
      value = uniformReal(0, 1);
    }
    for (int y = 0; y < m_noise.height; ++y) {
      for (int x = 0; x < m_noise.width; ++x) {
        if (back.at(m_noise.width - 1 - x, y) == kNoText) {
          continue;
        }
        const double fx = double(x) / cell;
        const double fy = double(y) / cell;
        const int cx = int(fx);
        const int cy = int(fy);
        const double tx = fx - cx;
        const double ty = fy - cy;
        const auto f = [&](int i, int j) { return field[size_t(cy + j) * cols + cx + i]; };
        const double value = (f(0, 0) * (1 - tx) + f(1, 0) * tx) * (1 - ty) + (f(0, 1) * (1 - tx) + f(1, 1) * tx) * ty;
        if (value > 0.72) {
          put(x, y, kShowThrough);
        }
      }
    }
  }

 private:
  // A noise pixel must not touch text, or it would become part of a text component.
  bool canPlace(const int x, const int y) const {
    for (int dy = -1; dy <= 1; ++dy) {
      for (int dx = -1; dx <= 1; ++dx) {
        if (!m_text.contains(x + dx, y + dy)) {
          if ((dx == 0) && (dy == 0)) {
            return false;
          }
          continue;
        }
        if (m_text.at(x + dx, y + dy) != kNoText) {
          return false;
        }
      }
    }
    return true;
  }

  void put(const int x, const int y, const uint8_t cls) {
    if (canPlace(x, y) && (m_noise.at(x, y) == kNoNoise)) {
      m_noise.at(x, y) = cls;
    }
  }

  // A random clump of about 'area' pixels grown from 'start'.
  void blob(const QPoint start, const int area, const uint8_t cls) {
    if (!canPlace(start.x(), start.y())) {
      return;
    }
    std::vector<QPoint> points{start};
    for (int tries = 0; (int(points.size()) < area) && (tries < area * 20); ++tries) {
      const QPoint p = points[uniform(0, int(points.size()) - 1)];
      const QPoint q(p.x() + uniform(-1, 1), p.y() + uniform(-1, 1));
      if (canPlace(q.x(), q.y()) && (std::find(points.begin(), points.end(), q) == points.end())) {
        points.push_back(q);
      }
    }
    for (const QPoint& p : points) {
      put(p.x(), p.y(), cls);
    }
  }

  int scaledArea(const int area) const { return std::max(1, int(std::lround(area * m_scale * m_scale))); }

  QPoint randomPoint() { return {uniform(0, m_noise.width - 1), uniform(0, m_noise.height - 1)}; }

  int uniform(const int lo, const int hi) { return std::uniform_int_distribution<int>(lo, hi)(m_rng); }

  double uniformReal(const double lo, const double hi) { return std::uniform_real_distribution<double>(lo, hi)(m_rng); }

  const Layer& m_text;
  Layer& m_noise;
  std::mt19937 m_rng;
  double m_scale;
};

struct Page {
  Page(int w, int h) : text(w, h), noise(w, h) { text.part.assign(size_t(w) * h, 0); }

  Layer text;
  Layer noise;
};

int mmToPx(const double mm, const int dpi) {
  return int(std::lround(mm * dpi / 25.4));
}

Page makePage(const QFont& baseFont, const int pointSize, const int dpi, const uint32_t seed) {
  Page page(mmToPx(130, dpi), mmToPx(185, dpi));
  QFont font(baseFont);
  font.setPixelSize(int(std::lround(pointSize * dpi / 72.0)));
  Typesetter typesetter(font);

  const int left = mmToPx(18, dpi);
  const int top = mmToPx(18, dpi);
  const int maxWidth = page.text.width - 2 * left;
  const int maxLines = (page.text.height - 2 * top) / typesetter.lineSpacing();
  // Like a scan: the threshold varies a little over the page and from pixel to pixel, so that the
  // edges are ragged and equal dots come out in different sizes.
  std::mt19937 inkRng(seed ^ 0x5bd1e995u);
  const int pageShift = std::uniform_int_distribution<int>(-kPageShift, kPageShift)(inkRng);
  std::uniform_int_distribution<int> pixelNoise(-kPixelNoise, kPixelNoise);
  const auto inked = [&](const int ink) { return ink + pageShift + pixelNoise(inkRng) >= 128; };
  const auto drawText = [&](Layer& layer, const int firstLine) {
    const std::vector<PageText> lines = composeText(typesetter, maxWidth, maxLines + firstLine);
    for (int i = 0; i < maxLines; ++i) {
      const PageText& line = lines[i + firstLine];
      const int baseline = top + typesetter.ascent() + i * typesetter.lineSpacing();
      typesetter.draw(line.left, left, baseline, layer, inked);
      typesetter.draw(line.right, left + maxWidth - typesetter.width(line.right), baseline, layer, inked);
    }
  };
  drawText(page.text, 0);
  // The back of the sheet: the same kind of text, starting elsewhere.
  Layer back(page.text.width, page.text.height);
  drawText(back, 5);

  const double scale = dpi / 300.0;
  const double areaScale = scale * scale;
  NoiseMaker noise(page.text, page.noise, seed, scale);
  noise.showThrough(back, mmToPx(1.5, dpi));
  noise.border(mmToPx(5, dpi), 250);
  noise.dust(250);
  noise.fibres(40, dpi);
  noise.nearText(300);
  noise.salt(int(1500 * areaScale));
  return page;
}

BinaryImage toBinary(const Page& page) {
  BinaryImage image(page.text.width, page.text.height, WHITE);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if ((page.text.at(x, y) != kNoText) || (page.noise.at(x, y) != kNoNoise)) {
        image.setPixel(x, y, BLACK);
      }
    }
  }
  return image;
}

struct Stats {
  std::array<long long, kTextClasses> textComponents{};
  std::array<long long, kTextClasses> textLost{};
  long long textPixels = 0;
  long long textPixelsLost = 0;
  std::array<long long, kNoiseClasses> noiseComponents{};
  std::array<long long, kNoiseClasses> noiseRemoved{};
  long long noisePixels = 0;
  long long noisePixelsRemoved = 0;
  double milliseconds = 0;
  int pages = 0;

  void add(const Stats& other) {
    for (int i = 0; i < kTextClasses; ++i) {
      textComponents[i] += other.textComponents[i];
      textLost[i] += other.textLost[i];
    }
    for (int i = 0; i < kNoiseClasses; ++i) {
      noiseComponents[i] += other.noiseComponents[i];
      noiseRemoved[i] += other.noiseRemoved[i];
    }
    textPixels += other.textPixels;
    textPixelsLost += other.textPixelsLost;
    noisePixels += other.noisePixels;
    noisePixelsRemoved += other.noisePixelsRemoved;
    milliseconds += other.milliseconds;
    pages += other.pages;
  }

  long long allNoise() const { return sum(noiseComponents); }

  long long allNoiseRemoved() const { return sum(noiseRemoved); }

  static long long sum(const std::array<long long, kNoiseClasses>& values) {
    long long total = 0;
    for (const long long v : values) {
      total += v;
    }
    return total;
  }
};

// Counts the text lost by glyph parts and the noise removed by components of the page. A component
// with any text pixel counts as text, any other one as noise of the kind of most of its pixels.
Stats measure(const Page& page,
              const BinaryImage& input,
              const BinaryImage& output,
              QImage* picture,
              std::vector<QPoint>* lostText) {
  const ConnectivityMap cmap(input, CONN8);
  const size_t count = cmap.maxLabel() + 1;
  std::vector<std::array<int, kTextClasses>> textPixels(count);
  std::vector<std::array<int, kNoiseClasses>> noisePixels(count);
  std::vector<int> kept(count, 0);
  const uint32_t* line = cmap.data();
  for (int y = 0; y < input.height(); ++y, line += cmap.stride()) {
    for (int x = 0; x < input.width(); ++x) {
      const uint32_t label = line[x];
      if (label == 0) {
        continue;
      }
      ++textPixels[label][page.text.at(x, y)];
      ++noisePixels[label][page.noise.at(x, y)];
      if (output.getPixel(x, y) == BLACK) {
        ++kept[label];
      }
    }
  }

  Stats stats;

  // Text by glyph parts: a part is lost when more than half of its pixels are. Thresholding may
  // break a letter into pieces; losing a one-pixel splinter of it does not count as losing it.
  struct Part {
    int pixels = 0;
    int removed = 0;
    uint8_t cls = kNoText;
    QPoint where;
  };
  std::vector<Part> parts(page.text.nextPart);
  for (int y = 0; y < input.height(); ++y) {
    for (int x = 0; x < input.width(); ++x) {
      const uint32_t id = page.text.part[size_t(y) * page.text.width + x];
      if ((id == 0) || (input.getPixel(x, y) != BLACK)) {
        continue;
      }
      Part& part = parts[id];
      ++part.pixels;
      part.cls = page.text.at(x, y);
      part.where = QPoint(x, y);
      ++stats.textPixels;
      if (output.getPixel(x, y) != BLACK) {
        ++part.removed;
        ++stats.textPixelsLost;
      }
    }
  }
  for (const Part& part : parts) {
    if (part.pixels == 0) {
      continue;
    }
    ++stats.textComponents[part.cls];
    if (2 * part.removed > part.pixels) {
      ++stats.textLost[part.cls];
      if (lostText) {
        lostText->push_back(part.where);
      }
    }
  }

  // Noise by components of the page; a component with any text pixel is text.
  std::vector<uint8_t> textClass(count, kNoText);
  for (size_t label = 1; label < count; ++label) {
    int size = 0;
    int best = 0;
    for (int c = 1; c < kTextClasses; ++c) {
      size += textPixels[label][c];
      if (textPixels[label][c] > textPixels[label][best]) {
        best = c;
      }
    }
    const bool removed = kept[label] == 0;
    if (size > 0) {
      textClass[label] = uint8_t(best);
      continue;
    }
    for (int c = 1; c < kNoiseClasses; ++c) {
      size += noisePixels[label][c];
      if (noisePixels[label][c] > noisePixels[label][best]) {
        best = c;
      }
    }
    ++stats.noiseComponents[best];
    stats.noisePixels += size;
    if (removed) {
      ++stats.noiseRemoved[best];
      stats.noisePixelsRemoved += size;
    }
  }

  if (picture) {
    *picture = QImage(input.width(), input.height(), QImage::Format_RGB32);
    picture->fill(Qt::white);
    line = cmap.data();
    for (int y = 0; y < input.height(); ++y, line += cmap.stride()) {
      for (int x = 0; x < input.width(); ++x) {
        const uint32_t label = line[x];
        if (label == 0) {
          continue;
        }
        const bool isKept = kept[label] > 0;
        if (textClass[label] != kNoText) {
          picture->setPixel(x, y, isKept ? qRgb(0, 0, 0) : qRgb(0, 90, 255));
        } else {
          picture->setPixel(x, y, isKept ? qRgb(230, 0, 0) : qRgb(205, 205, 205));
        }
      }
    }
  }
  return stats;
}

QString percent(const long long part, const long long whole) {
  return (whole == 0) ? QStringLiteral("-") : QString::number(100.0 * part / whole, 'f', 1) + u'%';
}

QString lostOf(const long long lost, const long long total) {
  return QString::number(lost) + u'/' + QString::number(total);
}

struct FontChoice {
  const char* family;
  const char* name;
};
}  // namespace

int bench(const QString& outDir, const bool images) {
  QDir().mkpath(outDir);
  QFile csvFile(QDir(outDir).filePath("bench.csv"));
  if (!csvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
    std::cerr << "cannot write " << csvFile.fileName().toStdString() << "\n";
    return 1;
  }
  QTextStream csv(&csvFile);
  csv << "font,pt,dpi,level,ms";
  for (int c = 1; c < kTextClasses; ++c) {
    csv << ",text " << kTextClassNames[c] << ",lost";
  }
  csv << ",text px,text px lost";
  for (int c = 1; c < kNoiseClasses; ++c) {
    csv << ",noise " << kNoiseClassNames[c] << ",removed";
  }
  csv << ",noise px,noise px removed\n";

  const FontChoice fonts[] = {{"Times New Roman", "serif"}, {"Arial", "sans"}};
  const int pointSizes[] = {6, 8, 10, 12, 14};
  const int resolutions[] = {200, 300, 400, 600};

  std::map<std::pair<int, int>, Stats> byLevelAndDpi;  // (level index, dpi)
  std::array<Stats, 3> byLevel;
  for (const FontChoice& choice : fonts) {
    const uint32_t fontSeed = uint32_t(&choice - fonts) * 7;
    const QFont font(QString::fromLatin1(choice.family));
    std::cout << choice.name << ": " << QFontInfo(font).family().toStdString() << std::endl;
    for (const int pointSize : pointSizes) {
      for (const int dpi : resolutions) {
        const uint32_t seed = uint32_t(pointSize * 1000 + dpi) + fontSeed;
        const Page page = makePage(font, pointSize, dpi, seed);
        const BinaryImage input = toBinary(page);
        const QString pageName = QStringLiteral("%1_%2pt_%3dpi").arg(choice.name).arg(pointSize).arg(dpi);
        for (int level = 0; level < 3; ++level) {
          QElapsedTimer timer;
          timer.start();
          const BinaryImage output = Despeckle::despeckle(input, Dpi(dpi, dpi), kLevels[level].level, NullTaskStatus());
          const double milliseconds = timer.nsecsElapsed() / 1e6;

          QImage picture;
          std::vector<QPoint> lostText;
          Stats stats = measure(page, input, output, images ? &picture : nullptr, images ? &lostText : nullptr);
          stats.milliseconds = milliseconds;
          stats.pages = 1;
          byLevelAndDpi[{level, dpi}].add(stats);
          byLevel[level].add(stats);

          csv << choice.name << ',' << pointSize << ',' << dpi << ',' << kLevels[level].name << ','
              << QString::number(milliseconds, 'f', 1);
          for (int c = 1; c < kTextClasses; ++c) {
            csv << ',' << stats.textComponents[c] << ',' << stats.textLost[c];
          }
          csv << ',' << stats.textPixels << ',' << stats.textPixelsLost;
          for (int c = 1; c < kNoiseClasses; ++c) {
            csv << ',' << stats.noiseComponents[c] << ',' << stats.noiseRemoved[c];
          }
          csv << ',' << stats.noisePixels << ',' << stats.noisePixelsRemoved << '\n';

          if (images) {
            const QString base = QDir(outDir).filePath(pageName + "_" + kLevels[level].name);
            if (kLevels[level].level == Despeckle::NORMAL) {
              picture.save(base + ".png");
            }
            saveSpotSheet(picture, lostText, base + "_lost.png");
          }
        }
        std::cout << pageName.toStdString() << " done" << std::endl;
      }
    }
  }

  std::cout << "\nText components lost (lost/total) and noise components removed, summed over fonts and sizes\n";
  std::cout << "level       dpi  noise removed  noise px removed";
  for (int c = 1; c < kTextClasses; ++c) {
    std::cout << " | " << kTextClassNames[c];
  }
  std::cout << " | text px lost | ms/page\n";
  for (int level = 0; level < 3; ++level) {
    const auto printRow = [&](const QString& dpi, const Stats& s) {
      QString row = QStringLiteral("%1 %2  %3  %4")
                        .arg(QString::fromLatin1(kLevels[level].name), -10)
                        .arg(dpi, 4)
                        .arg(percent(s.allNoiseRemoved(), s.allNoise()), 13)
                        .arg(percent(s.noisePixelsRemoved, s.noisePixels), 16);
      for (int c = 1; c < kTextClasses; ++c) {
        row += QStringLiteral(" | %1").arg(lostOf(s.textLost[c], s.textComponents[c]),
                                           int(std::strlen(kTextClassNames[c])));
      }
      row += QStringLiteral(" | %1 | %2")
                 .arg(percent(s.textPixelsLost, s.textPixels), 12)
                 .arg(s.milliseconds / std::max(1, s.pages), 7, 'f', 0);
      std::cout << row.toStdString() << "\n";
    };
    for (const int dpi : resolutions) {
      printRow(QString::number(dpi), byLevelAndDpi[{level, dpi}]);
    }
    printRow(QStringLiteral("all"), byLevel[level]);
  }

  std::cout << "\nNoise components removed by kind\nlevel     ";
  for (int c = 1; c < kNoiseClasses; ++c) {
    std::cout << " | " << kNoiseClassNames[c];
  }
  std::cout << "\n";
  for (int level = 0; level < 3; ++level) {
    QString row = QString::fromLatin1(kLevels[level].name).leftJustified(10);
    for (int c = 1; c < kNoiseClasses; ++c) {
      row += QStringLiteral(" | %1").arg(percent(byLevel[level].noiseRemoved[c], byLevel[level].noiseComponents[c]),
                                         std::max(6, int(std::strlen(kNoiseClassNames[c]))));
    }
    std::cout << row.toStdString() << "\n";
  }
  return 0;
}
