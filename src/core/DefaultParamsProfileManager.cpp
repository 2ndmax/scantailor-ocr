// Copyright (C) 2019  Joseph Artsimovich <joseph.artsimovich@gmail.com>, 4lex4 <4lex49@zoho.com>
// Use of this source code is governed by the GNU GPLv3 license that can be found in the LICENSE file.

#include "DefaultParamsProfileManager.h"

#include <config.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QtXml/QDomDocument>
#include <algorithm>

#include "DefaultParams.h"
#include "SettingsSetup.h"
#include "version.h"

using namespace page_split;
using namespace output;
using namespace page_layout;

namespace {
bool isValidProfileName(const QString& name) {
  if (name.isEmpty() || (name == QLatin1String(".")) || (name == QLatin1String(".."))) {
    return false;
  }
  for (const QChar ch : name) {
    if ((ch == QLatin1Char('/')) || (ch == QLatin1Char('\\')) || (ch == QLatin1Char(':')) || ch.isNull()) {
      return false;
    }
  }
  return true;
}
}  // namespace

DefaultParamsProfileManager::DefaultParamsProfileManager() {
  const QString portableConfigPath = settings_setup::portableConfigPath();
  if (!portableConfigPath.isNull()) {
    m_path = portableConfigPath + "/profiles";
  } else {
    const QString appDataLocation = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (!appDataLocation.isEmpty()) {
      m_path = appDataLocation + "/profiles";
    }
  }
}

DefaultParamsProfileManager::DefaultParamsProfileManager(const QString& path) : m_path(path) {}

std::list<QString> DefaultParamsProfileManager::getProfileList() const {
  std::list<QString> profileList;
  if (m_path.isEmpty()) {
    return profileList;
  }

  QDir dir(m_path);
  if (dir.exists()) {
    QList<QFileInfo> fileInfoList = dir.entryInfoList();
    for (const QFileInfo& fileInfo : fileInfoList) {
      if (fileInfo.isFile() && ((fileInfo.suffix() == "stp") || (fileInfo.suffix() == "xml"))) {
        // A profile can have both files.
        const QString name = fileInfo.completeBaseName();
        if (std::find(profileList.begin(), profileList.end(), name) == profileList.end()) {
          profileList.push_back(name);
        }
      }
    }
  }
  return profileList;
}

QFileInfo DefaultParamsProfileManager::profileFile(const QString& name) const {
  if (m_path.isEmpty() || !isValidProfileName(name)) {
    return QFileInfo();
  }
  const QDir dir(m_path);
  // .xml is the format of older versions; once saved again, the profile is a .stp file.
  const QFileInfo stp(dir.absoluteFilePath(name + ".stp"));
  return stp.exists() ? stp : QFileInfo(dir.absoluteFilePath(name + ".xml"));
}

std::unique_ptr<DefaultParams> DefaultParamsProfileManager::readProfile(const QString& name, LoadStatus* status) const {
  const QFileInfo profile = profileFile(name);
  if (profile.filePath().isEmpty() || !profile.exists()) {
    if (status) {
      *status = IO_ERROR;
    }
    return nullptr;
  }

  QFile profileFile(profile.filePath());
  if (!profileFile.open(QIODevice::ReadOnly)) {
    if (status) {
      *status = IO_ERROR;
    }
    return nullptr;
  }
  return readProfile(profileFile, status);
}

std::unique_ptr<DefaultParams> DefaultParamsProfileManager::readProfile(QIODevice& device, LoadStatus* status) {
  QDomDocument doc;
  if (!doc.setContent(&device)) {
    if (status) {
      *status = IO_ERROR;
    }
    return nullptr;
  }

  device.close();

  const QDomElement profileElement(doc.documentElement());
  if (profileElement.tagName() != QLatin1String("profile")) {
    if (status) {
      *status = IO_ERROR;
    }
    return nullptr;
  }
  // Older versions are fine, the defaults fill in what they lack. Profiles are saved as PROJECT_VERSION,
  // so accepting only that one would drop all of them with the next version.
  bool versionOk = false;
  const int version = profileElement.attribute("version").toInt(&versionOk);
  if (!versionOk || (version < PROJECT_VERSION_UPSTREAM_OLD) || (version > PROJECT_VERSION)) {
    if (status) {
      *status = INCOMPATIBLE_VERSION_ERROR;
    }
    return nullptr;
  }
  const QDomElement defaultParamsElement(profileElement.namedItem("default-params").toElement());
  if (defaultParamsElement.isNull()) {
    if (status) {
      *status = IO_ERROR;
    }
    return nullptr;
  }

  if (status) {
    *status = SUCCESS;
  }
  return std::make_unique<DefaultParams>(defaultParamsElement);
}

bool DefaultParamsProfileManager::writeProfile(const DefaultParams& params, const QString& name) const {
  if (m_path.isEmpty() || !isValidProfileName(name)) {
    return false;
  }
  QDomDocument doc;
  QDomElement rootElement(doc.createElement("profile"));
  doc.appendChild(rootElement);
  rootElement.setAttribute("version", PROJECT_VERSION);
  rootElement.appendChild(params.toXml(doc, "default-params"));

  QDir dir(m_path);
  if (!dir.exists() && !dir.mkpath(".")) {
    return false;
  }

  // Replaces an existing profile only once the new one has been written completely.
  QSaveFile file(dir.absoluteFilePath(name + ".stp"));
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }
  QTextStream textStream(&file);
  doc.save(textStream, 2);
  textStream.flush();
  return file.commit();
}

std::unique_ptr<DefaultParams> DefaultParamsProfileManager::createDefaultProfile() const {
  return std::make_unique<DefaultParams>();
}

std::unique_ptr<DefaultParams> DefaultParamsProfileManager::createMinimalProfile() const {
  DefaultParams::DeskewParams deskewParams;
  deskewParams.setMode(MODE_MANUAL);

  DefaultParams::PageSplitParams pageSplitParams;
  pageSplitParams.setLayoutType(SINGLE_PAGE_UNCUT);

  DefaultParams::SelectContentParams selectContentParams;
  selectContentParams.setContentDetectEnabled(false);

  DefaultParams::PageLayoutParams pageLayoutParams;
  pageLayoutParams.setHardMargins(Margins(0, 0, 0, 0));

  Alignment alignment;
  alignment.setNull(true);
  pageLayoutParams.setAlignment(alignment);

  DefaultParams::OutputParams outputParams;

  ColorParams colorParams;
  colorParams.setColorMode(COLOR_GRAYSCALE);

  ColorCommonOptions colorCommonOptions;
  colorCommonOptions.setFillMargins(false);
  colorParams.setColorCommonOptions(colorCommonOptions);

  outputParams.setColorParams(colorParams);
  return std::make_unique<DefaultParams>(DefaultParams::FixOrientationParams(), deskewParams, pageSplitParams,
                                         selectContentParams, pageLayoutParams, outputParams);
}

DefaultParamsProfileManager::StartProfile DefaultParamsProfileManager::startProfile(const QString& storedName) const {
  StartProfile start;
  if (storedName == "Default") {
    start.params = createDefaultProfile();
    start.name = storedName;
  } else if ((storedName == "Minimal") || (storedName == "Source")) {
    // "Source" is the former name of the minimal profile.
    start.params = createMinimalProfile();
    start.name = "Minimal";
  } else {
    start.params = readProfile(storedName);
    if (start.params != nullptr) {
      start.name = storedName;
    } else {
      start.params = createDefaultProfile();
      start.name = "Default";
      if (profileFile(storedName).exists()) {
        // Locked for a moment (virus scanner, cloud sync), damaged or from a newer version:
        // the defaults are used for this session, the user's choice is kept for the next start.
        start.storedName = storedName;
        return start;
      }
    }
  }
  start.storedName = start.name;
  return start;
}

bool DefaultParamsProfileManager::deleteProfile(const QString& name) const {
  if (m_path.isEmpty() || !isValidProfileName(name)) {
    return false;
  }
  // Both files go, or the old .xml one would take the place of the deleted profile.
  const QDir dir(m_path);
  bool found = false;
  for (const QString& suffix : {QStringLiteral(".stp"), QStringLiteral(".xml")}) {
    QFile file(dir.absoluteFilePath(name + suffix));
    if (file.exists()) {
      found = true;
      if (!file.remove()) {
        return false;
      }
    }
  }
  return found;
}
