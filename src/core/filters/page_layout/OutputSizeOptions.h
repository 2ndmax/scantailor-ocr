// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_PAGE_LAYOUT_OUTPUTSIZEOPTIONS_H_
#define SCANTAILOR_PAGE_LAYOUT_OUTPUTSIZEOPTIONS_H_

#include <QSizeF>

class QDomDocument;
class QDomElement;
class QString;

namespace page_layout {
/**
 * \brief The output size of the pages matched to others, for the whole project.
 *
 * Without an own target size, the target is the common size (the largest width
 * and height of the matched pages). The limits apply to pages that are scaled to
 * fit; pages filled with margins are only reduced, never enlarged.
 * Adopted from the idea of ScanTailor Experimental ("match size mode").
 */
class OutputSizeOptions {
 public:
  static constexpr int DEFAULT_MAX_ENLARGE_PERCENT = 150;
  static constexpr int DEFAULT_MIN_REDUCE_PERCENT = 50;

  OutputSizeOptions() = default;

  explicit OutputSizeOptions(const QDomElement& el);

  QDomElement toXml(QDomDocument& doc, const QString& name) const;

  bool operator==(const OutputSizeOptions& other) const;

  bool operator!=(const OutputSizeOptions& other) const;

  /** Nothing to store. */
  bool isDefault() const;

  bool hasOwnTargetSize() const { return m_ownTargetSize; }

  void setOwnTargetSize(bool own) { m_ownTargetSize = own; }

  /** The own target size in millimeters, including the margins. */
  const QSizeF& targetSizeMM() const { return m_targetSizeMm; }

  void setTargetSizeMM(const QSizeF& sizeMm) { m_targetSizeMm = sizeMm; }

  /** Enlarge to at most this percentage. */
  int maxEnlargePercent() const { return m_maxEnlargePercent; }

  void setMaxEnlargePercent(int percent);

  /** Reduce to at least this percentage. */
  int minReducePercent() const { return m_minReducePercent; }

  void setMinReducePercent(int percent);

  /** The own target size if there is one, otherwise \p commonSizeMm. */
  QSizeF targetSizeMM(const QSizeF& commonSizeMm) const;

 private:
  bool m_ownTargetSize = false;
  QSizeF m_targetSizeMm;
  int m_maxEnlargePercent = DEFAULT_MAX_ENLARGE_PERCENT;
  int m_minReducePercent = DEFAULT_MIN_REDUCE_PERCENT;
};
}  // namespace page_layout
#endif  // SCANTAILOR_PAGE_LAYOUT_OUTPUTSIZEOPTIONS_H_
