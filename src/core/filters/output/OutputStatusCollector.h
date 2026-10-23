// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_OUTPUT_OUTPUTSTATUSCOLLECTOR_H_
#define SCANTAILOR_OUTPUT_OUTPUTSTATUSCOLLECTOR_H_

#include "AbstractFilterDataCollector.h"

namespace output {
/**
 * Collects whether the output files of a page match its current settings, the same check that
 * decides between the output thumbnail and the hourglass in the Output step. Passed through the
 * chain of cache driven tasks; stays "not up to date" if a step before has not been done for the
 * page yet.
 */
class OutputStatusCollector : public AbstractFilterDataCollector {
 public:
  void setUpToDate(const bool upToDate) { m_upToDate = upToDate; }

  bool isUpToDate() const { return m_upToDate; }

 private:
  bool m_upToDate = false;
};
}  // namespace output
#endif  // SCANTAILOR_OUTPUT_OUTPUTSTATUSCOLLECTOR_H_
