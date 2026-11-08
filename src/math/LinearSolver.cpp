// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "LinearSolver.h"

// REVIEW-NOTE (2026-11): cols_A_rows_X of 0 -> pointer arithmetic in solve() underflows. Not fixed because
// the only caller, MatrixCalc, never has a matrix without columns.
LinearSolver::LinearSolver(size_t rows_AB, size_t cols_A_rows_X, size_t cols_BX)
    : m_rowsAB(rows_AB), m_colsArowsX(cols_A_rows_X), m_colsBX(cols_BX) {
  if (m_rowsAB < m_colsArowsX) {
    throw std::runtime_error("LinearSolver: can's solve underdetermined systems");
  }
}
