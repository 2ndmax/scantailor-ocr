// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "OutputParams.h"

#include <QDir>
#include <QDomDocument>
#include <QFileInfo>

#include "FillZoneComparator.h"
#include "FillZonePropFactory.h"
#include "PictureZoneComparator.h"
#include "PictureZonePropFactory.h"
#include "RenderParams.h"
#include "Utils.h"

namespace output {
OutputParams::OutputParams(const OutputImageParams& outputImageParams,
                           const OutputFileParams& sourceFileParams,
                           const OutputFileParams& outputFileParams,
                           const OutputFileParams& foregroundFileParams,
                           const OutputFileParams& backgroundFileParams,
                           const OutputFileParams& originalBackgroundFileParams,
                           const OutputFileParams& automaskFileParams,
                           const OutputFileParams& specklesFileParams,
                           const ZoneSet& pictureZones,
                           const ZoneSet& fillZones)
    : m_outputImageParams(outputImageParams),
      m_sourceFileParams(sourceFileParams),
      m_outputFileParams(outputFileParams),
      m_foregroundFileParams(foregroundFileParams),
      m_backgroundFileParams(backgroundFileParams),
      m_originalBackgroundFileParams(originalBackgroundFileParams),
      m_automaskFileParams(automaskFileParams),
      m_specklesFileParams(specklesFileParams),
      m_pictureZones(pictureZones),
      m_fillZones(fillZones) {}

OutputParams::OutputParams(const QDomElement& el)
    : m_outputImageParams(el.namedItem("image").toElement()),
      m_sourceFileParams(el.namedItem("source_file").toElement()),
      m_outputFileParams(el.namedItem("file").toElement()),
      m_foregroundFileParams(el.namedItem("foreground_file").toElement()),
      m_backgroundFileParams(el.namedItem("background_file").toElement()),
      m_originalBackgroundFileParams(el.namedItem("original_background_file").toElement()),
      m_automaskFileParams(el.namedItem("automask").toElement()),
      m_specklesFileParams(el.namedItem("speckles").toElement()),
      m_pictureZones(el.namedItem("zones").toElement(), PictureZonePropFactory()),
      m_fillZones(el.namedItem("fill-zones").toElement(), FillZonePropFactory()) {}

QDomElement OutputParams::toXml(QDomDocument& doc, const QString& name) const {
  QDomElement el(doc.createElement(name));
  el.appendChild(m_outputImageParams.toXml(doc, "image"));
  el.appendChild(m_sourceFileParams.toXml(doc, "source_file"));
  el.appendChild(m_outputFileParams.toXml(doc, "file"));
  el.appendChild(m_foregroundFileParams.toXml(doc, "foreground_file"));
  el.appendChild(m_backgroundFileParams.toXml(doc, "background_file"));
  el.appendChild(m_originalBackgroundFileParams.toXml(doc, "original_background_file"));
  el.appendChild(m_automaskFileParams.toXml(doc, "automask"));
  el.appendChild(m_specklesFileParams.toXml(doc, "speckles"));
  el.appendChild(m_pictureZones.toXml(doc, "zones"));
  el.appendChild(m_fillZones.toXml(doc, "fill-zones"));
  return el;
}

bool OutputParams::isUpToDate(const OutputImageParams& newOutputImageParams,
                              const ZoneSet& newPictureZones,
                              const ZoneSet& newFillZones,
                              const QFileInfo& sourceFileInfo,
                              const QFileInfo& outFileInfo,
                              const QString& outDir,
                              const RenderParams& renderParams) const {
  if (!m_outputImageParams.matches(newOutputImageParams)) {
    return false;
  }
  if (!PictureZoneComparator::equal(m_pictureZones, newPictureZones)) {
    return false;
  }
  if (!FillZoneComparator::equal(m_fillZones, newFillZones)) {
    return false;
  }
  if (!m_sourceFileParams.matches(OutputFileParams(sourceFileInfo))) {
    return false;
  }

  if (!renderParams.splitOutput()) {
    return outFileInfo.exists() && m_outputFileParams.matches(OutputFileParams(outFileInfo));
  }

  const QFileInfo foregroundFileInfo(QDir(Utils::foregroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
  const QFileInfo backgroundFileInfo(QDir(Utils::backgroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
  if (!foregroundFileInfo.exists() || !backgroundFileInfo.exists()) {
    return false;
  }
  if (!m_foregroundFileParams.matches(OutputFileParams(foregroundFileInfo))
      || !m_backgroundFileParams.matches(OutputFileParams(backgroundFileInfo))) {
    return false;
  }
  if (renderParams.originalBackground()) {
    const QFileInfo originalBackgroundFileInfo(
        QDir(Utils::originalBackgroundDir(outDir)).absoluteFilePath(outFileInfo.fileName()));
    if (!originalBackgroundFileInfo.exists()
        || !m_originalBackgroundFileParams.matches(OutputFileParams(originalBackgroundFileInfo))) {
      return false;
    }
  }
  return true;
}
}  // namespace output
