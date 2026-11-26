// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "PropertySet.h"

#include <QDomDocument>
#include <algorithm>
#include <utility>

#include "PropertyFactory.h"

PropertySet::PropertySet(const QDomElement& el, const PropertyFactory& factory) {
  const QString propertyStr("property");
  QDomNode node(el.firstChild());

  for (; !node.isNull(); node = node.nextSibling()) {
    if (!node.isElement()) {
      continue;
    }
    if (node.nodeName() != propertyStr) {
      continue;
    }

    QDomElement propEl(node.toElement());
    std::shared_ptr<Property> prop = factory.construct(propEl);
    if (prop) {
      // typeid of a named reference, so the dereference is not evaluated inside typeid.
      const Property& propRef = *prop;
      setProperty(typeid(propRef), std::move(prop));
    }
  }
}

PropertySet::PropertySet(const PropertySet& other) {
  m_props.reserve(other.m_props.size());
  for (const auto& [type, prop] : other.m_props) {
    m_props.emplace_back(type, prop->clone());
  }
}

PropertySet& PropertySet::operator=(const PropertySet& other) {
  PropertySet(other).swap(*this);
  return *this;
}

void PropertySet::setProperty(const std::type_index type, std::shared_ptr<Property> prop) {
  const auto it
      = std::find_if(m_props.begin(), m_props.end(), [type](const auto& entry) { return entry.first == type; });
  if (it != m_props.end()) {
    it->second = std::move(prop);
  } else {
    m_props.emplace_back(type, std::move(prop));
  }
}

QDomElement PropertySet::toXml(QDomDocument& doc, const QString& name) const {
  const QString propertyStr("property");
  QDomElement propsEl(doc.createElement(name));

  for (const auto& [type, prop] : m_props) {
    propsEl.appendChild(prop->toXml(doc, propertyStr));
  }
  return propsEl;
}
