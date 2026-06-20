/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "DaemonIpcSecurity.h"

#include "base/Log.h"
#include "common/Constants.h"

#include <QLocalSocket>

#if defined(Q_OS_WIN)
#include "arch/win32/XArchWindows.h"
#include "platform/MSWindowsHandle.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Wtsapi32.h>

#include <string>
#endif

namespace deskflow::core::ipc {

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

bool isAuthorizedDaemonIpcClient(QLocalSocket *clientSocket)
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

} // namespace deskflow::core::ipc
