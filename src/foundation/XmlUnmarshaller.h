// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#ifndef SCANTAILOR_FOUNDATION_XMLUNMARSHALLER_H_
#define SCANTAILOR_FOUNDATION_XMLUNMARSHALLER_H_

class QString;
class QDomElement;
class QSize;
class QSizeF;
class QPointF;
class QLineF;
class QRect;
class QRectF;
class QPolygonF;

class XmlUnmarshaller {
 public:
  /**
   * The attribute \p name as a number, or \p fallback if it is missing, isn't a number or isn't finite.
   * A damaged or hand-edited project file may hold "nan" or "inf", which a range check like
   * "value < 0 || value > 1" lets through, as does std::clamp().
   */
  static double number(const QDomElement& el, const QString& name, double fallback = 0.0);

  /** As number(), limited to [\p min, \p max]; the fallback is limited as well. */
  static double numberInRange(const QDomElement& el,
                              const QString& name,
                              double min,
                              double max,
                              double fallback = 0.0);

  static QString string(const QDomElement& el);

  static QSize size(const QDomElement& el);

  static QSizeF sizeF(const QDomElement& el);

  static QPointF pointF(const QDomElement& el);

  static QLineF lineF(const QDomElement& el);

  static QRect rect(const QDomElement& el);

  static QRectF rectF(const QDomElement& el);

  static QPolygonF polygonF(const QDomElement& el);
};


#endif  // ifndef SCANTAILOR_FOUNDATION_XMLUNMARSHALLER_H_
