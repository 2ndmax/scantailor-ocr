// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_CONSTANTS_H_
#define SCANTAILOR_FOUNDATION_CONSTANTS_H_

// Constant expressions, not variables that are set while the program starts: another static
// initialization in the program could otherwise read them before that and get 0.
namespace constants {
inline constexpr double PI = 3.141592653589793238462643383279502884;

inline constexpr double SQRT_2 = 1.4142135623730950488016887242097;

/**
 * angle_rad = angle_deg * RED2RAD
 */
inline constexpr double DEG2RAD = PI / 180.0;

/**
 * angle_deg = angle_rad * RAD2DEG
 */
inline constexpr double RAD2DEG = 180.0 / PI;

/**
 * mm = inch * INCH2MM
 */
inline constexpr double INCH2MM = 25.4;

/**
 * inch = mm * MM2INCH
 */
inline constexpr double MM2INCH = 1.0 / INCH2MM;

/**
 * dots_per_inch = dots_per_meter * DPM2DPI
 */
inline constexpr double DPM2DPI = 0.0254;

/**
 * dots_per_meter = dots_per_inch * DPI2DPM
 */
inline constexpr double DPI2DPM = 1.0 / DPM2DPI;
}  // namespace constants
#endif
