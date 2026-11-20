// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_DOWNSCALEDIMAGE_H_
#define SCANTAILOR_CORE_DOWNSCALEDIMAGE_H_

#include <QImage>

/**
 * \brief A stand-alone function to create a downscaled image
 *        to be passed to the constructor of ImageViewBase.
 *
 * The point of using this function instead of letting
 * the constructor do the job is that this function may
 * be called from a background thread, while the constructor
 * can't.
 *
 * \param image The input image, not null, and with DPI set correctly.
 * \return The image downscaled by an unspecified degree.
 */
QImage createDownscaledImage(const QImage& image);

#endif  // ifndef SCANTAILOR_CORE_DOWNSCALEDIMAGE_H_
