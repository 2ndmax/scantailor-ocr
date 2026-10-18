// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_DEWARPING_WIDTHCORRECTION_H_
#define SCANTAILOR_DEWARPING_WIDTHCORRECTION_H_

#include <vector>

class QDomDocument;
class QDomElement;
class QString;

namespace dewarping {
/**
 * \brief Redistributes the horizontal positions of a dewarped page.
 *
 * The dewarper describes a horizontal position on the dewarped page by a
 * number from 0 (left edge of the model) to 1 (right edge). A width
 * correction maps such a position to a corrected one, stretching areas where
 * the letters came out too narrow and compressing those where they came out
 * too wide. It is monotonic and keeps 0 and 1 in place, so the size of the
 * page does not change. Outside [0, 1] it is the identity.
 *
 * A null correction (the default) changes nothing.
 */
class WidthCorrection {
 public:
  WidthCorrection() = default;

  /**
   * \param samples The corrected positions of 0, 1/(n-1), ..., 1. They have to
   *        rise strictly from 0 to 1, otherwise the correction is null.
   */
  explicit WidthCorrection(std::vector<double> samples);

  explicit WidthCorrection(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool isNull() const { return m_samples.empty(); }

  const std::vector<double>& samples() const { return m_samples; }

  /** \brief Uncorrected position -> corrected position. */
  double map(double x) const;

  /** \brief Corrected position -> uncorrected position. */
  double unmap(double x) const;

  bool matches(const WidthCorrection& other) const;

 private:
  static bool isValid(const std::vector<double>& samples);

  std::vector<double> m_samples;
};
}  // namespace dewarping

#endif  // SCANTAILOR_DEWARPING_WIDTHCORRECTION_H_
