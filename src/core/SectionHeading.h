// Copyright (C) 2026  ScanTailor OCR contributors
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_CORE_SECTIONHEADING_H_
#define SCANTAILOR_CORE_SECTIONHEADING_H_

#include <QLabel>

/**
 * \brief The bold heading of a part of a panel in the options of a step.
 *
 * A panel (a CollapsibleGroupBox) holds everything that one "Apply To ..." button applies.
 * Its parts are divided by these headings: left aligned, without a frame and not collapsible.
 */
class SectionHeading : public QLabel {
  Q_OBJECT

  /** The first heading of a panel sits right below the title; the others get space above. */
  Q_PROPERTY(bool first READ isFirst WRITE setFirst)

 public:
  explicit SectionHeading(QWidget* parent = nullptr);

  explicit SectionHeading(const QString& text, QWidget* parent = nullptr);

  bool isFirst() const;

  void setFirst(bool first);

 private:
  void initialize();

  bool m_first = false;
};

#endif  // SCANTAILOR_CORE_SECTIONHEADING_H_
