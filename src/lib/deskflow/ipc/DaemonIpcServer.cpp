/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025-2026 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonIpcServer.h"

#include "base/Log.h"
#include "common/Constants.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocalSocket>

#include <string>

#if defined(Q_OS_WIN)
#include "arch/win32/XArchWindows.h"
#include "platform/MSWindowsHandle.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Wtsapi32.h>
#endif

namespace deskflow::core::ipc {

const auto kAckMessage = "ok";
const auto kErrorMessage = "error";

#if defined(Q_OS_WIN)
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
  return normalizedPath(lhs).compare(normalizedPath(rhs), Qt::CaseInsensitive) == 0;
}

bool readTokenUserSid(HANDLE token, QByteArray &sidBuffer, PSID &sid)
{
  DWORD requiredBytes = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &requiredBytes);
  if (requiredBytes == 0 && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    LOG_WARN("could not query token user size, error: %s", windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  sidBuffer.resize(static_cast<int>(requiredBytes));
  if (!GetTokenInformation(
          token, TokenUser, sidBuffer.data(), static_cast<DWORD>(sidBuffer.size()), &requiredBytes
      )) {
    LOG_WARN("could not query token user, error: %s", windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  sid = reinterpret_cast<TOKEN_USER *>(sidBuffer.data())->User.Sid;
  if (!IsValidSid(sid)) {
    LOG_WARN("token user sid is invalid");
    return false;
  }

  return true;
}

bool readProcessUserSid(DWORD processId, QByteArray &sidBuffer, PSID &sid)
{
  MSWindowsHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
  if (process.get() == nullptr) {
    LOG_WARN("could not open ipc client process %lu, error: %s", processId, windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  HANDLE token = nullptr;
  if (!OpenProcessToken(process.get(), TOKEN_QUERY, &token)) {
    LOG_WARN(
        "could not open ipc client process token %lu, error: %s", processId,
        windowsErrorToString(GetLastError()).c_str()
    );
    return false;
  }

  MSWindowsHandle tokenHandle(token);
  return readTokenUserSid(tokenHandle.get(), sidBuffer, sid);
}

bool readActiveUserSid(DWORD activeSessionId, QByteArray &sidBuffer, PSID &sid)
{
  HANDLE token = nullptr;
  if (WTSQueryUserToken(activeSessionId, &token)) {
    MSWindowsHandle tokenHandle(token);
    return readTokenUserSid(tokenHandle.get(), sidBuffer, sid);
  }

  LOG_DEBUG(
      "could not query active user token, falling back to daemon user token, error: %s",
      windowsErrorToString(GetLastError()).c_str()
  );

  if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
    LOG_WARN("could not open daemon process token, error: %s", windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  MSWindowsHandle tokenHandle(token);
  return readTokenUserSid(tokenHandle.get(), sidBuffer, sid);
}

bool isExpectedGuiProcess(DWORD processId)
{
  MSWindowsHandle process(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId));
  if (process.get() == nullptr) {
    LOG_WARN("could not open ipc client process %lu, error: %s", processId, windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  std::wstring processPath(32768, L'\0');
  DWORD processPathSize = static_cast<DWORD>(processPath.size());
  if (!QueryFullProcessImageNameW(process.get(), 0, processPath.data(), &processPathSize)) {
    LOG_WARN(
        "could not query ipc client process image %lu, error: %s", processId,
        windowsErrorToString(GetLastError()).c_str()
    );
    return false;
  }

  processPath.resize(processPathSize);
  const auto actualPath = QString::fromStdWString(processPath);
  const auto expectedPath = QStringLiteral("%1/%2.exe").arg(QCoreApplication::applicationDirPath(), kAppId);

  if (!pathsMatch(actualPath, expectedPath)) {
    LOG_WARN(
        "ipc client process image is not allowed: pid=%lu path=%s expected=%s", processId,
        qPrintable(normalizedPath(actualPath)), qPrintable(normalizedPath(expectedPath))
    );
    return false;
  }

  return true;
}

} // namespace
#endif

DaemonIpcServer::DaemonIpcServer(QObject *parent, const QString &logFilename)
    : IpcServer(parent, kDaemonIpcName, QStringLiteral("daemon")),
      m_logFilename(logFilename)
{
  // do nothing
}

bool DaemonIpcServer::authorizeClient(QLocalSocket *clientSocket)
{
#if defined(Q_OS_WIN)
  const auto descriptor = clientSocket->socketDescriptor();
  const auto pipeHandle = reinterpret_cast<HANDLE>(descriptor);
  if (descriptor == -1 || pipeHandle == nullptr || pipeHandle == INVALID_HANDLE_VALUE) {
    LOG_WARN("ipc client authorization failed: invalid socket descriptor");
    return false;
  }

  ULONG clientProcessId = 0;
  if (!GetNamedPipeClientProcessId(pipeHandle, &clientProcessId)) {
    LOG_WARN("could not query ipc client process id, error: %s", windowsErrorToString(GetLastError()).c_str());
    return false;
  }

  const DWORD activeSessionId = WTSGetActiveConsoleSessionId();
  if (activeSessionId == 0xFFFFFFFF) {
    LOG_WARN("ipc client authorization failed: no active console session");
    return false;
  }

  DWORD clientSessionId = 0;
  if (!ProcessIdToSessionId(clientProcessId, &clientSessionId)) {
    LOG_WARN(
        "could not query ipc client session id, pid=%lu, error: %s", clientProcessId,
        windowsErrorToString(GetLastError()).c_str()
    );
    return false;
  }

  if (clientSessionId != activeSessionId) {
    LOG_WARN(
        "ipc client authorization failed: pid=%lu session=%lu activeSession=%lu", clientProcessId, clientSessionId,
        activeSessionId
    );
    return false;
  }

  if (!isExpectedGuiProcess(clientProcessId)) {
    return false;
  }

  QByteArray clientSidBuffer;
  PSID clientSid = nullptr;
  if (!readProcessUserSid(clientProcessId, clientSidBuffer, clientSid)) {
    return false;
  }

  QByteArray activeSidBuffer;
  PSID activeSid = nullptr;
  if (!readActiveUserSid(activeSessionId, activeSidBuffer, activeSid)) {
    return false;
  }

  if (!EqualSid(clientSid, activeSid)) {
    LOG_WARN("ipc client authorization failed: client user does not match active user");
    return false;
  }

  LOG_DEBUG("ipc client authorized: pid=%lu session=%lu", clientProcessId, clientSessionId);
#else
  Q_UNUSED(clientSocket)
#endif

  return true;
}

void DaemonIpcServer::processCommand(QLocalSocket *clientSocket, const QString &command, const QStringList &parts)
{
  if (command == QStringLiteral("logLevel")) {
    processLogLevel(clientSocket, parts);
  } else if (command == QStringLiteral("configFile")) {
    processConfigFile(clientSocket, parts);
  } else if (command == QStringLiteral("start")) {
    LOG_DEBUG("daemon ipc server got start message");
    Q_EMIT startProcessRequested();
    writeToClientSocket(clientSocket, kAckMessage);
  } else if (command == QStringLiteral("stop")) {
    LOG_DEBUG("daemon ipc server got stop message");
    Q_EMIT stopProcessRequested();
    writeToClientSocket(clientSocket, kAckMessage);
  } else if (command == QStringLiteral("logPath")) {
    LOG_DEBUG("daemon ipc server got log path request");
    writeToClientSocket(clientSocket, QStringLiteral("logPath=%1").arg(m_logFilename.toUtf8()));
  } else if (command == QStringLiteral("clearSettings")) {
    LOG_DEBUG("daemon ipc server got clear settings message");
    Q_EMIT clearSettingsRequested();
    writeToClientSocket(clientSocket, kAckMessage);
  } else {
    LOG_WARN("daemon ipc server got unknown command: %s", command.toUtf8().constData());
  }
}

void DaemonIpcServer::processLogLevel(QLocalSocket *&clientSocket, const QStringList &messageParts)
{
  if (messageParts.size() < 2) {
    LOG_ERR("daemon ipc server got invalid log level message");
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  const auto &logLevel = messageParts.at(1);
  if (logLevel.isEmpty()) {
    LOG_ERR("daemon ipc server got empty log level");
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  LOG_DEBUG("daemon ipc server got new log level: %s", logLevel.toUtf8().constData());
  Q_EMIT logLevelChanged(logLevel);
  writeToClientSocket(clientSocket, kAckMessage);
}

void DaemonIpcServer::processConfigFile(QLocalSocket *&clientSocket, const QStringList &messageParts)
{
  if (messageParts.size() < 2) {
    LOG_ERR("daemon ipc server got invalid config file message");
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  const auto &configFile = messageParts.at(1);
  if (configFile.isEmpty()) {
    LOG_ERR("daemon ipc server got empty config file path");
    writeToClientSocket(clientSocket, kErrorMessage);
    return;
  }

  LOG_DEBUG("daemon ipc server got config file: %s", configFile.toUtf8().constData());
  Q_EMIT configFileChanged(configFile);
  writeToClientSocket(clientSocket, kAckMessage);
}

} // namespace deskflow::core::ipc
