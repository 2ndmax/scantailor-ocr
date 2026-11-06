// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FIX_ORIENTATION_OPTIONSWIDGET_H_
#define SCANTAILOR_FIX_ORIENTATION_OPTIONSWIDGET_H_

#include <core/ConnectionManager.h>

#include <memory>

#include "FilterOptionsWidget.h"
#include "ImageTrim.h"
#include "OrthogonalRotation.h"
#include "PageId.h"
#include "PageInfo.h"
#include "PageSelectionAccessor.h"
#include "Units.h"
#include "UnitsListener.h"
#include "ui_OptionsWidget.h"

namespace fix_orientation {
class Settings;

class OptionsWidget : public FilterOptionsWidget, public UnitsListener, private Ui::OptionsWidget {
  Q_OBJECT
 public:
  OptionsWidget(std::shared_ptr<Settings> settings, const PageSelectionAccessor& pageSelectionAccessor);

  ~OptionsWidget() override;

  void preUpdateUI(const PageInfo& pageInfo, OrthogonalRotation rotation);

  OrthogonalRotation rotation() const;

  /** The trim of the current page, in source image pixels. */
  ImageTrim trim() const;

  void onUnitsChanged(Units units) override;

 signals:

  /** \p trim is in source image pixels. */
  void rotationOrTrimChanged(OrthogonalRotation rotation, const ImageTrim& trim);

 private slots:

  void rotateLeft();

  void rotateRight();

  void showApplyToDialog();

  void appliedTo(const std::set<PageId>& pages, bool applyRotation, bool applyTrim);

  void trimEnableToggled(bool checked);

  void trimMarginsChanged(double value);

 private:
  void setRotation(const OrthogonalRotation& rotation);

  void setRotationPixmap();

  void setupUiConnections();

  void setupIcons();

  void updateTrimMaximums();

  void pullTrimToControls();

  void pushTrimFromControls();

  // The edges refer to the view, not to the source image.
  ImageTrim currentTrimFromControls() const;

  // The resolution of the image as seen in the view: a quarter turn swaps its two directions.
  Dpi viewDpi() const;

  // The unit of the trim fields: pixels if the image has no resolution to convert with.
  Units fieldUnits() const;

  // A trim edge of \p pixels in the unit of the fields, and back, rounded to whole pixels.
  double pixelsToField(int pixels, Qt::Orientation direction) const;
  int fieldToPixels(double value, Qt::Orientation direction) const;

  void setTrimFieldMaximum(QDoubleSpinBox* spinBox, int pixels, Qt::Orientation direction);

  std::shared_ptr<Settings> m_settings;
  PageSelectionAccessor m_pageSelectionAccessor;
  PageId m_pageId;
  QSize m_imagePixelSize;
  Dpi m_imageDpi;
  OrthogonalRotation m_rotation;
  // The trim in the fields, in pixels of the view. The fields may show rounded values.
  ImageTrim m_viewTrim;

  ConnectionManager m_connectionManager;
};
}  // namespace fix_orientation
#endif  // ifndef SCANTAILOR_FIX_ORIENTATION_OPTIONSWIDGET_H_
