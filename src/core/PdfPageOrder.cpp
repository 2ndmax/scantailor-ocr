// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PdfPageOrder.h"

#include <QSet>
#include <algorithm>

std::vector<PdfPageOrder::Item> PdfPageOrder::merge(const std::vector<Item>& previous,
                                                    const std::vector<QString>& projectOrder) {
  QSet<QString> inProject;
  for (const QString& key : projectOrder) {
    inProject.insert(key);
  }

  // The pages that are still there, in their earlier order.
  std::vector<Item> result;
  QSet<QString> known;
  for (const Item& item : previous) {
    if (inProject.contains(item.key) && !known.contains(item.key)) {
      result.push_back(item);
      known.insert(item.key);
    }
  }

  // New pages go right after their predecessor in project order.  That one is already in
  // the result, as earlier new pages have been inserted before.
  for (size_t i = 0; i < projectOrder.size(); ++i) {
    const QString& key = projectOrder[i];
    if (known.contains(key)) {
      continue;
    }
    auto pos = result.begin();
    if (i > 0) {
      const QString& predecessor = projectOrder[i - 1];
      pos = std::find_if(result.begin(), result.end(), [&](const Item& item) { return item.key == predecessor; });
      if (pos != result.end()) {
        ++pos;
      }
    }
    result.insert(pos, Item{key, true});
    known.insert(key);
  }
  return result;
}
