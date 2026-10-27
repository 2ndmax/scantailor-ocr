// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "Report.h"

#include <QFile>
#include <QTextStream>
#include <algorithm>
#include <cmath>
#include <functional>

namespace compare {

namespace {
/** A false alarm is an angle on an unsheared page that is large enough to see. */
const double FALSE_ALARM_DEG = 0.1;

struct Summary {
  int count = 0;
  double mean = 0;
  /** The value that 95 % of the pages reach or beat. */
  double p95 = 0;
  double worst = 0;
};

Summary summarize(std::vector<double> values) {
  Summary summary;
  summary.count = static_cast<int>(values.size());
  if (values.empty()) {
    return summary;
  }
  std::sort(values.begin(), values.end());
  double sum = 0;
  for (const double v : values) {
    sum += v;
  }
  summary.mean = sum / values.size();
  const int n = static_cast<int>(values.size());
  // Nearest rank.
  summary.p95 = values[std::clamp(static_cast<int>(std::ceil(0.95 * n)) - 1, 0, n - 1)];
  summary.worst = values.back();
  return summary;
}

/** The key figures of one method over a group of pages. */
struct Figures {
  int pages = 0;
  /** Pages with a true shear, and how many of them got an angle. */
  int sheared = 0;
  int detected = 0;
  /** |shear left after the correction| over the sheared pages; no angle counts as 0°. */
  Summary residual;
  /** |found - true| over the sheared pages that got an angle. */
  Summary detectedError;
  /** Pages without shear, and how many of them got an angle of at least FALSE_ALARM_DEG. */
  int unsheared = 0;
  int falseAlarms = 0;
  double worstFalseAlarm = 0;
  /** |angle on the shifted scan - angle on the scan| over the pages with an angle on both. */
  Summary instability;
  /** Pages with an angle on only one of the scan and the shifted scan. */
  int flips = 0;
};

Figures figures(const std::vector<PageResult>& results,
                const int method,
                const std::function<bool(const PageSpec&)>& matches) {
  Figures f;
  std::vector<double> residual;
  std::vector<double> detectedError;
  std::vector<double> instability;
  for (const PageResult& r : results) {
    if (!matches(r.spec)) {
      continue;
    }
    ++f.pages;
    const MethodResult& m = r.methods[method];
    if (r.spec.shearClass == ShearClass::NONE) {
      ++f.unsheared;
      const double found = std::abs(m.shear.value_or(0.0));
      if (found >= FALSE_ALARM_DEG) {
        ++f.falseAlarms;
      }
      f.worstFalseAlarm = std::max(f.worstFalseAlarm, found);
    } else {
      ++f.sheared;
      residual.push_back(std::abs(r.residualShear(method)));
      if (m.shear) {
        ++f.detected;
        detectedError.push_back(std::abs(r.residualShear(method)));
      }
    }
    if (m.shear && m.shearShifted) {
      instability.push_back(std::abs(*m.shearShifted - *m.shear));
    } else if (m.shear.has_value() != m.shearShifted.has_value()) {
      ++f.flips;
    }
  }
  f.residual = summarize(residual);
  f.detectedError = summarize(detectedError);
  f.instability = summarize(instability);
  return f;
}

struct Group {
  QString name;
  std::function<bool(const PageSpec&)> matches;
};

std::vector<Group> groups() {
  std::vector<Group> result;
  result.push_back({QStringLiteral("all pages"), [](const PageSpec&) { return true; }});
  for (const ShearClass c : {ShearClass::NONE, ShearClass::SMALL, ShearClass::LARGE}) {
    result.push_back({QStringLiteral("shear ") + toString(c), [c](const PageSpec& s) { return s.shearClass == c; }});
  }
  result.push_back({QStringLiteral("shear 0°, no keystone"),
                    [](const PageSpec& s) { return (s.shearClass == ShearClass::NONE) && (s.keystone == 0); }});
  result.push_back({QStringLiteral("keystone (shear 0°)"), [](const PageSpec& s) { return s.keystone != 0; }});
  for (const Alignment a : {Alignment::JUSTIFIED, Alignment::RAGGED_RIGHT, Alignment::CENTERED}) {
    result.push_back({toString(a), [a](const PageSpec& s) { return s.alignment == a; }});
  }
  for (const TextAmount text : {TextAmount::FULL_PAGE, TextAmount::HALF_PAGE, TextAmount::FEW_LINES}) {
    result.push_back({toString(text), [text](const PageSpec& s) { return s.text == text; }});
  }
  for (const Layout layout : {Layout::ONE_COLUMN, Layout::TWO_COLUMNS, Layout::TEXT_WITH_PICTURE}) {
    result.push_back({toString(layout), [layout](const PageSpec& s) { return s.layout == layout; }});
  }
  for (const int dpi : {150, 300, 600}) {
    result.push_back({QStringLiteral("%1 dpi").arg(dpi), [dpi](const PageSpec& s) { return s.dpi == dpi; }});
  }
  for (const Disturbance d : {Disturbance::NONE, Disturbance::NOISE, Disturbance::BLUR}) {
    result.push_back(
        {QStringLiteral("disturbance: ") + toString(d), [d](const PageSpec& s) { return s.disturbance == d; }});
  }
  result.push_back({QStringLiteral("sans serif"), [](const PageSpec& s) { return !s.serif; }});
  result.push_back({QStringLiteral("serif"), [](const PageSpec& s) { return s.serif; }});
  for (const Italic i : {Italic::NONE, Italic::WORDS, Italic::PAGE}) {
    result.push_back({toString(i), [i](const PageSpec& s) { return s.italic == i; }});
  }
  result.push_back({QStringLiteral("header and footer"), [](const PageSpec& s) { return s.headerFooter; }});
  result.push_back({QStringLiteral("indents"), [](const PageSpec& s) { return s.indents; }});
  result.push_back({QStringLiteral("lists"), [](const PageSpec& s) { return s.lists; }});
  result.push_back({QStringLiteral("ragged right, one column, keystone"), [](const PageSpec& s) {
                      return (s.alignment == Alignment::RAGGED_RIGHT) && (s.layout == Layout::ONE_COLUMN)
                             && (s.keystone != 0);
                    }});
  return result;
}

QString number(const double value, const int decimals) {
  return QString::number(value, 'f', decimals);
}

QString degrees(const double value) {
  return number(value, 2) + QStringLiteral("°");
}

QString share(const int part, const int total) {
  if (total == 0) {
    return QStringLiteral("–");
  }
  return QStringLiteral("%1 of %2 (%3 %)").arg(part).arg(total).arg(number(100.0 * part / total, 0));
}

QString triple(const Summary& s) {
  if (s.count == 0) {
    return QStringLiteral("–");
  }
  return QStringLiteral("%1 / %2 / %3").arg(degrees(s.mean), degrees(s.p95), degrees(s.worst));
}

/** The columns of the tables, one row per group. */
const char* const COLUMNS[] = {"Group",
                               "Pages",
                               "detected (sheared pages)",
                               "residual shear mean / 95 % / worst",
                               "error when detected",
                               "false alarms at 0° (from 0.1°)",
                               "instability on shift",
                               "flips"};
const int NUM_COLUMNS = sizeof(COLUMNS) / sizeof(COLUMNS[0]);

std::vector<QString> row(const QString& name, const Figures& f) {
  QString alarms = share(f.falseAlarms, f.unsheared);
  if (f.unsheared > 0) {
    alarms += QStringLiteral(", largest ") + degrees(f.worstFalseAlarm);
  }
  return {name,   QString::number(f.pages), share(f.detected, f.sheared), triple(f.residual), triple(f.detectedError),
          alarms, triple(f.instability),    QString::number(f.flips)};
}

std::vector<QString> conclusions(const std::vector<PageResult>& results, const RunInfo& info) {
  std::vector<QString> lines;
  std::vector<double> rotationError;
  int rotationMissing = 0;
  for (const PageResult& r : results) {
    if (r.rotation) {
      rotationError.push_back(std::abs(*r.rotation - r.spec.rotationDeg));
    } else {
      ++rotationMissing;
    }
  }
  const Summary rot(summarize(rotationError));
  lines.push_back(QStringLiteral("Rotation (the basis of the shear measurement): error %1 (mean / 95 % / "
                                 "worst), no angle on %2 of %3 pages.")
                      .arg(triple(rot))
                      .arg(rotationMissing)
                      .arg(results.size()));

  const auto ofClass = [](const ShearClass c) { return [c](const PageSpec& s) { return s.shearClass == c; }; };
  for (size_t m = 0; m < info.methods.size(); ++m) {
    const int method = static_cast<int>(m);
    const Figures all(figures(results, method, [](const PageSpec&) { return true; }));
    const Figures small(figures(results, method, ofClass(ShearClass::SMALL)));
    const Figures large(figures(results, method, ofClass(ShearClass::LARGE)));
    lines.push_back(QStringLiteral("Shear, %1: detected %2 of the sheared pages (0.1°–1°: %3; 1°–4°: %4). "
                                   "Residual shear after \"Auto\" %5 on average, %6 in the worst case. "
                                   "False alarms at 0°: %7. Instability on a shift by fractions of a pixel: %8, "
                                   "flips on %9 pages.")
                        .arg(info.methods[m], share(all.detected, all.sheared), share(small.detected, small.sheared),
                             share(large.detected, large.sheared), degrees(all.residual.mean),
                             degrees(all.residual.worst), share(all.falseAlarms, all.unsheared),
                             triple(all.instability), QString::number(all.flips)));
  }
  return lines;
}

QString hint(const RunInfo& info) {
  return QStringLiteral(
             "%1 synthetic pages, seed %2, font %3, run time %4 s. The true angles are random, on no grid "
             "(rotation ±5°, shear 0° / 0.1°–1° / 1°–4°; half of the pages with 0° have a keystone of 1–6 % "
             "instead, which is no shear). Also random: serif font, italics (single "
             "words or the whole page), headers and footers, indents, lists. Residual shear: what is left after the "
             "correction"
             "; without a detected angle the whole shear is left. Instability: the difference of the angle when "
             "the same scan is moved by (%5; %6) pixels; flips: an angle on only one of the two scans.")
      .arg(info.pages)
      .arg(info.seed)
      .arg(info.fontFamily)
      .arg(std::lround(info.seconds))
      .arg(number(info.shift.x(), 2), number(info.shift.y(), 2));
}

QString textSummary(const std::vector<PageResult>& results, const RunInfo& info) {
  QString text;
  QTextStream out(&text);
  out << "Shear Detection Comparison\n" << hint(info) << "\n";
  const std::vector<Group> allGroups(groups());
  static const int WIDTHS[] = {24, 7, 26, 30, 30, 34, 30, 6};
  for (size_t m = 0; m < info.methods.size(); ++m) {
    out << "\nMethod: " << info.methods[m] << "\n";
    for (int c = 0; c < NUM_COLUMNS; ++c) {
      out << QString::fromUtf8(COLUMNS[c]).left(WIDTHS[c]).leftJustified(WIDTHS[c])
          << (c + 1 < NUM_COLUMNS ? " | " : "\n");
    }
    for (const Group& group : allGroups) {
      const std::vector<QString> cells(row(group.name, figures(results, static_cast<int>(m), group.matches)));
      for (int c = 0; c < NUM_COLUMNS; ++c) {
        out << cells[c].leftJustified(WIDTHS[c]) << (c + 1 < NUM_COLUMNS ? " | " : "\n");
      }
    }
  }
  out << "\nComputing time per page (mean, milliseconds; parallel runs slow each other down a little)\n";
  for (const int dpi : {150, 300, 600}) {
    out << dpi << " dpi:";
    for (size_t m = 0; m < info.methods.size(); ++m) {
      double sum = 0;
      int count = 0;
      for (const PageResult& r : results) {
        if (r.spec.dpi == dpi) {
          sum += r.methods[m].milliseconds;
          ++count;
        }
      }
      out << " | " << info.methods[m] << " " << ((count > 0) ? QString::number(std::lround(sum / count)) : "–");
    }
    out << "\n";
  }
  out << "\nConclusions\n";
  for (const QString& line : conclusions(results, info)) {
    out << "- " << line << "\n";
  }
  return text;
}

QString html(const std::vector<PageResult>& results, const std::vector<CaseImage>& cases, const RunInfo& info) {
  QString html;
  QTextStream out(&html);
  out << "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<title>Shear Detection Comparison</title><style>"
         "body{font-family:sans-serif;margin:24px;color:#222;background:#fff}"
         "table{border-collapse:collapse;margin:8px 0 20px}th,td{border:1px solid #ccc;padding:3px 7px;"
         "text-align:right}th{background:#f2f2f2}td:first-child,th:first-child{text-align:left}"
         ".conclusions li{margin:4px 0}figure{display:inline-block;margin:8px;vertical-align:top;max-width:100%}"
         "figure img{max-width:100%;border:1px solid #ccc}figcaption{font-size:13px;max-width:640px}"
         ".hint{color:#555;font-size:14px}</style></head><body>\n";
  out << "<h1>Shear Detection Comparison</h1>\n<p class=\"hint\">" << hint(info).toHtmlEscaped() << "</p>\n";
  out << "<h2>Conclusions</h2>\n<ul class=\"conclusions\">\n";
  for (const QString& line : conclusions(results, info)) {
    out << "<li>" << line.toHtmlEscaped() << "</li>\n";
  }
  out << "</ul>\n";

  const std::vector<Group> allGroups(groups());
  for (size_t m = 0; m < info.methods.size(); ++m) {
    out << "<h2>Key Figures: " << info.methods[m].toHtmlEscaped() << "</h2>\n<table><thead><tr>";
    for (const char* column : COLUMNS) {
      out << "<th>" << QString::fromUtf8(column).toHtmlEscaped() << "</th>";
    }
    out << "</tr></thead><tbody>\n";
    for (const Group& group : allGroups) {
      out << "<tr>";
      for (const QString& cell : row(group.name, figures(results, static_cast<int>(m), group.matches))) {
        out << "<td>" << cell.toHtmlEscaped() << "</td>";
      }
      out << "</tr>\n";
    }
    out << "</tbody></table>\n";
  }

  out << "<h2>Notable Cases</h2>\n<p class=\"hint\">The pages with the largest residual shear. Green: the true "
         "vertical; red: the vertical after the detected angle.</p>\n";
  for (size_t m = 0; m < info.methods.size(); ++m) {
    out << "<h3>" << info.methods[m].toHtmlEscaped() << "</h3>\n";
    for (const CaseImage& c : cases) {
      if (c.method == static_cast<int>(m)) {
        out << "<figure><img src=\"" << c.file.toHtmlEscaped() << "\" alt=\"\"><figcaption>"
            << c.caption.toHtmlEscaped() << "</figcaption></figure>\n";
      }
    }
  }
  out << "</body></html>\n";
  return html;
}

QString csvNumber(const std::optional<double>& value) {
  return value ? QString::number(*value, 'f', 4) : QString();
}

QString csv(const std::vector<PageResult>& results, const RunInfo& info) {
  QString text;
  QTextStream out(&text);
  out << "page;text;layout;alignment;dpi;disturbance;class;keystone;rotation_true;rotation_detected;method;shear_true;"
         "detected;detected_shifted;confidence;residual_shear;font;italic;header_footer;indents;lists\n";
  for (const PageResult& r : results) {
    const PageSpec& s = r.spec;
    for (size_t m = 0; m < info.methods.size(); ++m) {
      const MethodResult& mr = r.methods[m];
      out << s.index << ";" << toString(s.text) << ";" << toString(s.layout) << ";" << toString(s.alignment) << ";"
          << s.dpi << ";" << toString(s.disturbance) << ";" << toString(s.shearClass) << ";" << csvNumber(s.keystone)
          << ";" << csvNumber(s.rotationDeg) << ";" << csvNumber(r.rotation) << ";" << info.methods[m] << ";"
          << csvNumber(s.shearDeg) << ";" << csvNumber(mr.shear) << ";" << csvNumber(mr.shearShifted) << ";"
          << csvNumber(mr.confidence) << ";" << csvNumber(r.residualShear(static_cast<int>(m))) << ";"
          << (s.serif ? "serif" : "sans serif") << ";" << toString(s.italic) << ";" << (s.headerFooter ? 1 : 0) << ";"
          << (s.indents ? 1 : 0) << ";" << (s.lists ? 1 : 0) << "\n";
    }
  }
  return text;
}

bool writeText(const QString& path, const QString& text, QString* error) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    *error = QStringLiteral("Cannot write %1: %2").arg(path, file.errorString());
    return false;
  }
  file.write(text.toUtf8());
  return true;
}
}  // namespace

bool writeReport(const QString& outDir,
                 const std::vector<PageResult>& results,
                 const std::vector<CaseImage>& cases,
                 const RunInfo& info,
                 QString* error) {
  return writeText(outDir + QStringLiteral("/report.html"), html(results, cases, info), error)
         && writeText(outDir + QStringLiteral("/results.csv"), csv(results, info), error)
         && writeText(outDir + QStringLiteral("/summary.txt"), textSummary(results, info), error);
}

}  // namespace compare
