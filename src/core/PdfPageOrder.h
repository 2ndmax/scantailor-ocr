// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_PDFPAGEORDER_H_
#define SCANTAILOR_CORE_PDFPAGEORDER_H_

#include <QString>
#include <vector>

/**
 * \brief The order and the ticks of the pages in the "Create PDF" step.
 *
 * They are kept while the project is open, but the pages of the project may change in
 * between: pages can be added, removed or renamed (e.g. split into a left and a right page).
 */
class PdfPageOrder {
 public:
  struct Item {
    /** Identifies the page, e.g. the path of its output file. */
    QString key;
    bool checked = true;

    bool operator==(const Item& other) const { return (key == other.key) && (checked == other.checked); }
  };

  /**
   * Brings an earlier order up to date with the pages of the project.
   *
   * Pages that are still there keep their place and their tick.  Pages that are gone are
   * dropped.  New pages are ticked and put right after the page that precedes them in
   * \p projectOrder (at the start if there is none), so without any changes by the user
   * the result is the project order.
   *
   * \param previous The earlier order, as shown to the user.
   * \param projectOrder The keys of the pages of the project, in project order.
   */
  static std::vector<Item> merge(const std::vector<Item>& previous, const std::vector<QString>& projectOrder);
};


#endif  // SCANTAILOR_CORE_PDFPAGEORDER_H_
