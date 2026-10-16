// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "SmartFilenameOrdering.h"

#include <QFileInfo>
#include <QString>

bool SmartFilenameOrdering::operator()(const QFileInfo& lhs, const QFileInfo& rhs) const {
  // First compare directories.
  if (int comp = lhs.absolutePath().compare(rhs.absolutePath())) {
    return comp < 0;
  }

  const QString lhsFname(lhs.fileName());
  const QString rhsFname(rhs.fileName());
  const QChar* lhsPtr = lhsFname.constData();
  const QChar* rhsPtr = rhsFname.constData();
  while (!lhsPtr->isNull() && !rhsPtr->isNull()) {
    const bool lhsIsDigit = lhsPtr->isDigit();
    const bool rhsIsDigit = rhsPtr->isDigit();
    if (lhsIsDigit != rhsIsDigit) {
      // Digits have priority over non-digits.
      return lhsIsDigit;
    }

    if (lhsIsDigit && rhsIsDigit) {
      // The numbers are compared digit by digit, as long ones such as time stamps don't fit
      // an integer type.  Note: isDigit() implies !isNull().
      const QChar* lhsDigits = lhsPtr;
      while (lhsPtr->isDigit()) {
        ++lhsPtr;
      }
      const QChar* rhsDigits = rhsPtr;
      while (rhsPtr->isDigit()) {
        ++rhsPtr;
      }
      // Leading zeros don't count, so without them the longer number is the larger one.
      while ((lhsDigits + 1 < lhsPtr) && (lhsDigits->digitValue() == 0)) {
        ++lhsDigits;
      }
      while ((rhsDigits + 1 < rhsPtr) && (rhsDigits->digitValue() == 0)) {
        ++rhsDigits;
      }
      if ((lhsPtr - lhsDigits) != (rhsPtr - rhsDigits)) {
        return (lhsPtr - lhsDigits) < (rhsPtr - rhsDigits);
      }
      for (; lhsDigits != lhsPtr; ++lhsDigits, ++rhsDigits) {
        if (lhsDigits->digitValue() != rhsDigits->digitValue()) {
          return lhsDigits->digitValue() < rhsDigits->digitValue();
        }
      }
      continue;
    }

    // Both characters are non-digits at this point.  Note that testing
    // isNull() here instead would be dead code, as the loop condition
    // already guarantees neither pointer is at the end of its string.
    if (*lhsPtr != *rhsPtr) {
      return *lhsPtr < *rhsPtr;
    }

    ++lhsPtr;
    ++rhsPtr;
  }

  if (!lhsPtr->isNull() || !rhsPtr->isNull()) {
    return lhsPtr->isNull();
  }

  // OK, the smart comparison indicates the file names are equal.
  // However, if they aren't symbol-to-symbol equal, we can't treat
  // them as equal, so let's do a usual comparision now.
  return lhsFname < rhsFname;
}  // ()

bool SmartFilenameOrdering::operator()(QString const& lhs, QString const& rhs) const {
  QFileInfo lhs_i(lhs);
  QFileInfo rhs_i(rhs);
  return this->operator()(lhs_i, rhs_i);
}
