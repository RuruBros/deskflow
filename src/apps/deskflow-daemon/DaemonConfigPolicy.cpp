/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonConfigPolicy.h"

#include "arch/win32/XArchWindows.h"
#include "base/Log.h"
#include "common/Constants.h"
#include "common/Settings.h"
#include "platform/MSWindowsHandle.h"

#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <UserEnv.h>
#include <Wtsapi32.h>

#include <string>
#include <utility>

namespace deskflow::core {
namespace {

QString normalizedPath(const QString &path)
{
  const QFileInfo fileInfo(path);
  auto normalized = fileInfo.canonicalFilePath();
  if (normalized.isEmpty()) {
    normalized = fileInfo.absoluteFilePath();
  }
  return QDir::cleanPath(normalized);
}

bool pathsMatch(const QString &lhs, const QString &rhs)
{
  const auto lhsCanonical = QFileInfo(lhs).canonicalFilePath();
  if (lhsCanonical.isEmpty()) {
    return false;
  }

  const auto rhsAbsolute = QFileInfo(rhs).absoluteFilePath();
  return QDir::cleanPath(lhsCanonical).compare(QDir::cleanPath(rhsAbsolute), Qt::CaseInsensitive) == 0;
}

bool hasReparsePoint(const QString &path)
{
  const auto nativePath = QDir::toNativeSeparators(path).toStdWString();
  const DWORD attrs = GetFileAttributesW(nativePath.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    LOG_WARN("could not query config file attributes: %s", windowsErrorToString(GetLastError()).c_str());
    return true;
  }

  return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

QString activeUserSettingsFile()
{
  const DWORD activeSessionId = WTSGetActiveConsoleSessionId();
  if (activeSessionId == 0xFFFFFFFF) {
    LOG_WARN("cannot resolve active user settings file: no active console session");
    return {};
  }

  HANDLE token = nullptr;
  if (!WTSQueryUserToken(activeSessionId, &token)) {
    LOG_DEBUG(
        "could not query active user token for config allowlist, falling back to daemon user dir, error: %s",
        windowsErrorToString(GetLastError()).c_str()
    );
    return Settings::UserSettingFile;
  }

  MSWindowsHandle tokenHandle(token);

  DWORD profilePathSize = 0;
  GetUserProfileDirectoryW(tokenHandle.get(), nullptr, &profilePathSize);
  if (profilePathSize == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    LOG_WARN("could not query active user profile size, error: %s", windowsErrorToString(GetLastError()).c_str());
    return {};
  }

  std::wstring profilePath(profilePathSize, L'\0');
  if (!GetUserProfileDirectoryW(tokenHandle.get(), profilePath.data(), &profilePathSize)) {
    LOG_WARN("could not query active user profile path, error: %s", windowsErrorToString(GetLastError()).c_str());
    return {};
  }

  if (profilePathSize > 0 && profilePath.at(profilePathSize - 1) == L'\0') {
    profilePath.resize(profilePathSize - 1);
  } else {
    profilePath.resize(profilePathSize);
  }

  return QStringLiteral("%1/AppData/Roaming/%2/%2.conf").arg(QString::fromStdWString(profilePath), kAppName);
}

} // namespace

bool isAllowedDaemonConfigFile(const QString &configFile)
{
  if (configFile.startsWith(QStringLiteral("\\\\")) || configFile.startsWith(QStringLiteral("//"))) {
    LOG_ERR("remote config file paths are not allowed: %s", qPrintable(configFile));
    return false;
  }

  const QFileInfo configFileInfo(configFile);
  if (!configFileInfo.isAbsolute()) {
    LOG_ERR("relative config file paths are not allowed: %s", qPrintable(configFile));
    return false;
  }

  if (!configFileInfo.exists() || !configFileInfo.isFile()) {
    LOG_ERR("config file does not exist or is not a regular file: %s", qPrintable(configFile));
    return false;
  }

  if (hasReparsePoint(configFile)) {
    LOG_ERR("config file reparse points are not allowed: %s", qPrintable(configFile));
    return false;
  }

  QStringList allowedFiles{
      Settings::SystemSettingFile,
      Settings::portableSettingsFile(),
      Settings::UserSettingFile,
  };

  if (const auto activeUserFile = activeUserSettingsFile(); !activeUserFile.isEmpty()) {
    allowedFiles.append(activeUserFile);
  }

  for (const auto &allowedFile : std::as_const(allowedFiles)) {
    if (pathsMatch(configFile, allowedFile)) {
      return true;
    }
  }

  LOG_ERR("config file path is not allowed: %s", qPrintable(normalizedPath(configFile)));
  return false;
}

} // namespace deskflow::core
