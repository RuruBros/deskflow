/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2024 Chris Rizzitello <sithord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2024 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Constants.h"
#include "common/ExitCodes.h"
#include "common/I18N.h"
#include "common/PlatformInfo.h"
#include "common/Settings.h"
#include "common/UrlConstants.h"
#include "common/VersionInfo.h"
#include "gui/Diagnostic.h"
#include "gui/MainWindow.h"
#include "gui/Messages.h"
#include "gui/StyleUtils.h"

#include <QApplication>
#include <QByteArray>
#include <QCommandLineParser>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QMessageBox>
#include <QSharedMemory>
#include <QTextStream>

#if defined(Q_OS_MACOS)
#include <Carbon/Carbon.h>
#include <cstdlib>
#endif

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <DbgHelp.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string_view>
#endif

#if defined(Q_OS_UNIX) && defined(QT_DEBUG)
#include <QLoggingCategory>
#endif

#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
#include "platform/XDGPortalRegistry.h"
#endif

using namespace deskflow::gui;

#if defined(Q_OS_MACOS)
bool checkMacAssistiveDevices();
#endif

const static auto kHeader = QStringLiteral("%1: %2\n").arg(kAppName, kDisplayVersion);

namespace {

constexpr auto kDiagnosticLogSizeLimit = 4 * 1024 * 1024;

QString guiDiagnosticLogFilename()
{
  return QStringLiteral("%1/deskflow-gui-diagnostics.log").arg(Settings::UserDir);
}

void rotateDiagnosticLogIfNeeded(const QString &filename)
{
  const QFileInfo fileInfo(filename);
  if (!fileInfo.exists() || fileInfo.size() <= kDiagnosticLogSizeLimit) {
    return;
  }

  const auto oldFilename = QStringLiteral("%1.1").arg(filename);
  QFile::remove(oldFilename);
  QFile::rename(filename, oldFilename);
}

void appendGuiDiagnosticLine(const QString &line)
{
  QDir().mkpath(Settings::UserDir);

  const auto filename = guiDiagnosticLogFilename();
  rotateDiagnosticLogIfNeeded(filename);

  QFile file(filename);
  if (!file.open(QFile::WriteOnly | QFile::Append | QFile::Text)) {
    return;
  }

  QTextStream(&file) << '[' << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << "] " << line << Qt::endl;
}

QString qtMessageTypeName(QtMsgType type)
{
  switch (type) {
  case QtDebugMsg:
    return QStringLiteral("DEBUG");
  case QtInfoMsg:
    return QStringLiteral("INFO");
  case QtWarningMsg:
    return QStringLiteral("WARNING");
  case QtCriticalMsg:
    return QStringLiteral("CRITICAL");
  case QtFatalMsg:
    return QStringLiteral("FATAL");
  }

  return QStringLiteral("UNKNOWN");
}

QString qtMessageFileLine(const QMessageLogContext &context)
{
  if (!context.file) {
    return {};
  }

  return QStringLiteral("%1:%2").arg(context.file, QString::number(context.line));
}

void persistentMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
  const auto fileLine = qtMessageFileLine(context);
  auto line = QStringLiteral("%1: %2").arg(qtMessageTypeName(type), message);

  if (!fileLine.isEmpty()) {
    line.append(QStringLiteral("\n\t%1").arg(fileLine));
  }

  appendGuiDiagnosticLine(line);
  deskflow::gui::messages::messageHandler(type, context, message);
}

#if defined(Q_OS_WIN)
std::array<wchar_t, MAX_PATH> g_crashLogPath{};
std::array<wchar_t, MAX_PATH> g_crashDumpPath{};

bool hasPlatformArgument(int argc, char *argv[])
{
  for (int i = 1; i < argc; ++i) {
    if (argv[i] == nullptr) {
      continue;
    }

    const std::string_view arg{argv[i]};
    if (arg == "-platform" || arg == "--platform" || arg.starts_with("-platform=") || arg.starts_with("--platform=")) {
      return true;
    }
  }

  return false;
}

void avoidWindowsDirectWriteFontEngineCrash(int argc, char *argv[])
{
  if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM") || hasPlatformArgument(argc, argv)) {
    return;
  }

  // DirectWrite can crash while handling legacy bitmap fonts such as Fixedsys.
  qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("windows:fontengine=freetype"));
}

void writeCrashLine(HANDLE file, const char *line)
{
  DWORD written = 0;
  WriteFile(file, line, static_cast<DWORD>(strlen(line)), &written, nullptr);
  WriteFile(file, "\r\n", 2, &written, nullptr);
}

void writeFormattedCrashLine(HANDLE file, const char *format, ...)
{
  std::array<char, 2048> buffer{};

  va_list args;
  va_start(args, format);
  vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);

  writeCrashLine(file, buffer.data());
}

std::array<char, MAX_PATH> narrowPath(const wchar_t *path)
{
  std::array<char, MAX_PATH> result{};
  WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), static_cast<int>(result.size()), nullptr, nullptr);
  return result;
}

void writeExceptionModule(HANDLE file, void *address)
{
  MEMORY_BASIC_INFORMATION memoryInfo{};
  if (VirtualQuery(address, &memoryInfo, sizeof(memoryInfo)) == 0 || memoryInfo.AllocationBase == nullptr) {
    writeFormattedCrashLine(file, "fault module: <unavailable>");
    return;
  }

  std::array<wchar_t, MAX_PATH> modulePath{};
  const auto moduleHandle = static_cast<HMODULE>(memoryInfo.AllocationBase);
  if (GetModuleFileNameW(moduleHandle, modulePath.data(), static_cast<DWORD>(modulePath.size())) == 0) {
    writeFormattedCrashLine(file, "fault module: <unavailable>");
    return;
  }

  const auto modulePathUtf8 = narrowPath(modulePath.data());
  const auto offset = reinterpret_cast<uintptr_t>(address) - reinterpret_cast<uintptr_t>(moduleHandle);
  writeFormattedCrashLine(file, "fault module: %s", modulePathUtf8.data());
  writeFormattedCrashLine(file, "fault module offset: 0x%p", reinterpret_cast<void *>(offset));
}

void writeExceptionContext(HANDLE file, CONTEXT *context)
{
  if (context == nullptr) {
    return;
  }

#if defined(_M_X64)
  writeFormattedCrashLine(file, "RIP: 0x%p", reinterpret_cast<void *>(context->Rip));
  writeFormattedCrashLine(file, "RSP: 0x%p", reinterpret_cast<void *>(context->Rsp));
  writeFormattedCrashLine(file, "RBP: 0x%p", reinterpret_cast<void *>(context->Rbp));
  writeFormattedCrashLine(file, "RAX: 0x%p", reinterpret_cast<void *>(context->Rax));
  writeFormattedCrashLine(file, "RBX: 0x%p", reinterpret_cast<void *>(context->Rbx));
  writeFormattedCrashLine(file, "RCX: 0x%p", reinterpret_cast<void *>(context->Rcx));
  writeFormattedCrashLine(file, "RDX: 0x%p", reinterpret_cast<void *>(context->Rdx));
#elif defined(_M_IX86)
  writeFormattedCrashLine(file, "EIP: 0x%p", reinterpret_cast<void *>(context->Eip));
  writeFormattedCrashLine(file, "ESP: 0x%p", reinterpret_cast<void *>(context->Esp));
  writeFormattedCrashLine(file, "EBP: 0x%p", reinterpret_cast<void *>(context->Ebp));
  writeFormattedCrashLine(file, "EAX: 0x%p", reinterpret_cast<void *>(context->Eax));
  writeFormattedCrashLine(file, "EBX: 0x%p", reinterpret_cast<void *>(context->Ebx));
  writeFormattedCrashLine(file, "ECX: 0x%p", reinterpret_cast<void *>(context->Ecx));
  writeFormattedCrashLine(file, "EDX: 0x%p", reinterpret_cast<void *>(context->Edx));
#endif
}

void writeMinidump(HANDLE logFile, EXCEPTION_POINTERS *exceptionPointers)
{
  const auto dumpFile =
      CreateFileW(g_crashDumpPath.data(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (dumpFile == INVALID_HANDLE_VALUE) {
    writeFormattedCrashLine(logFile, "minidump: failed to create file, error=%lu", GetLastError());
    return;
  }

  MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
  exceptionInfo.ThreadId = GetCurrentThreadId();
  exceptionInfo.ExceptionPointers = exceptionPointers;
  exceptionInfo.ClientPointers = FALSE;

  const auto dumpType =
      static_cast<MINIDUMP_TYPE>(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
  const auto ok = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), dumpFile, dumpType, &exceptionInfo, nullptr, nullptr
  );

  CloseHandle(dumpFile);

  const auto dumpPathUtf8 = narrowPath(g_crashDumpPath.data());
  writeFormattedCrashLine(logFile, "minidump: %s", ok ? "written" : "failed");
  writeFormattedCrashLine(logFile, "minidump path: %s", dumpPathUtf8.data());
  if (!ok) {
    writeFormattedCrashLine(logFile, "minidump error: %lu", GetLastError());
  }
}

LONG WINAPI writeWindowsCrashDiagnostics(EXCEPTION_POINTERS *exceptionPointers)
{
  const auto file = CreateFileW(
      g_crashLogPath.data(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr
  );

  if (file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_CONTINUE_SEARCH;
  }

  SYSTEMTIME time{};
  GetLocalTime(&time);

  writeCrashLine(file, "----- Deskflow GUI crash -----");
  writeFormattedCrashLine(
      file, "time: %04hu-%02hu-%02huT%02hu:%02hu:%02hu.%03hu", time.wYear, time.wMonth, time.wDay, time.wHour,
      time.wMinute, time.wSecond, time.wMilliseconds
  );
  writeFormattedCrashLine(file, "version: %s", kDisplayVersion);
  writeFormattedCrashLine(file, "process id: %lu", GetCurrentProcessId());
  writeFormattedCrashLine(file, "thread id: %lu", GetCurrentThreadId());

  if (exceptionPointers != nullptr && exceptionPointers->ExceptionRecord != nullptr) {
    const auto record = exceptionPointers->ExceptionRecord;
    writeFormattedCrashLine(file, "exception code: 0x%08lx", record->ExceptionCode);
    writeFormattedCrashLine(file, "exception flags: 0x%08lx", record->ExceptionFlags);
    writeFormattedCrashLine(file, "exception address: 0x%p", record->ExceptionAddress);
    writeExceptionModule(file, record->ExceptionAddress);
  }

  if (exceptionPointers != nullptr) {
    writeExceptionContext(file, exceptionPointers->ContextRecord);
    writeMinidump(file, exceptionPointers);
  }

  CloseHandle(file);
  return EXCEPTION_CONTINUE_SEARCH;
}

void initializeWindowsCrashDiagnostics()
{
  QDir().mkpath(Settings::UserDir);

  const auto timestamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
  const auto crashLogPath = QDir::toNativeSeparators(QStringLiteral("%1/deskflow-gui-crash.log").arg(Settings::UserDir));
  const auto crashDumpPath =
      QDir::toNativeSeparators(QStringLiteral("%1/deskflow-gui-%2.dmp").arg(Settings::UserDir, timestamp));

  crashLogPath.toWCharArray(g_crashLogPath.data());
  crashDumpPath.toWCharArray(g_crashDumpPath.data());

  SetUnhandledExceptionFilter(writeWindowsCrashDiagnostics);
}
#endif

} // namespace

int main(int argc, char *argv[])
{
#if defined(Q_OS_UNIX) && defined(QT_DEBUG)
  // Fixes Fedora bug where qDebug() messages aren't printed.
  QLoggingCategory::setFilterRules(QStringLiteral("*.debug=true\nqt.*=false"));
#endif

#if !defined(Q_OS_MAC) && !defined(Q_OS_WIN)
  deskflow::platform::setAppId();
#endif

#if defined(Q_OS_WIN)
  avoidWindowsDirectWriteFontEngineCrash(argc, argv);
#endif

  QCoreApplication::setApplicationName(kAppName);
  QCoreApplication::setOrganizationName(kAppName);
  QCoreApplication::setApplicationVersion(kVersion);
  QCoreApplication::setOrganizationDomain(kOrgDomain); // used in prefix, can't be a url
  QGuiApplication::setDesktopFileName(kRevFqdnName);

  QApplication app(argc, argv);

#if defined(Q_OS_WIN)
  initializeWindowsCrashDiagnostics();
#endif

  appendGuiDiagnosticLine(QStringLiteral("GUI diagnostics initialized: %1 v%2").arg(kAppName, kDisplayVersion));

  // Ensure the I18N object is made before strings
  QTextStream(stdout) << "initial language: " << I18N::currentLanguage() << '\n';

  // Add Command Line Options
  auto helpOption = QCommandLineOption({"h", "help"}, "Display Help on the command line");
  auto versionOption = QCommandLineOption({"v", "version"}, "Display version information");
  auto resetOption = QCommandLineOption("reset", "Reset all settings");

  QCommandLineParser parser;
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.addOption(helpOption);
  parser.addOption(versionOption);
  parser.addOption(resetOption);
  parser.parse(QCoreApplication::arguments());

  if (!parser.errorText().isEmpty()) {
    qCritical().noquote() << parser.errorText() << "\nUse --help for more information.";
    return s_exitArgs;
  }

  if (parser.isSet(helpOption)) {
    QTextStream(stdout) << kHeader << QStringLiteral("  %1\n\n").arg(kAppDescription)
                        << parser.helpText().replace(QApplication::applicationFilePath(), kAppId);
    return s_exitSuccess;
  }

  if (parser.isSet(versionOption)) {
    QTextStream(stdout) << kHeader << kCopyright << Qt::endl;
    return s_exitSuccess;
  }

  const auto shmId = QStringLiteral("%1-gui").arg(kAppId);
  // Create a shared memory segment with a unique key
  // This is to prevent a new instance from running if one is already running
  QSharedMemory sharedMemory(shmId);

  // Attempt to attach first and detach in order to clean up stale shm chunks
  // This can happen if the previous instance was killed or crashed
  if (sharedMemory.attach())
    sharedMemory.detach();

  // If we can create 1 byte of SHM we are the only instance
  if (!sharedMemory.create(1)) {
    // Ping the running instance to have it show itself
    QLocalSocket socket;
    socket.connectToServer(shmId, QLocalSocket::ReadOnly);
    if (!socket.waitForConnected()) {
      // If we can't connect to the other instance tell the user its running.
      // This should never happen but just incase we should show something
      QMessageBox::information(nullptr, kAppName, QObject::tr("%1 is already running").arg(kAppName));
    }
    socket.disconnectFromServer();
    return s_exitDuplicate;
  }

  if (!deskflow::platform::isMac() && qEnvironmentVariable("XDG_CURRENT_DESKTOP") != QLatin1String("KDE")) {
    QApplication::setStyle("fusion");
  }

  // Sets the fallback icon path and fallback theme
  updateIconTheme();

  qInstallMessageHandler(persistentMessageHandler);
  qInfo("%s v%s", kAppName, kDisplayVersion);

#if defined(Q_OS_MACOS)

  if (app.applicationDirPath().startsWith("/Volumes/")) {
    QString msgBody = QStringLiteral(
        "Please drag %1 to the Applications folder, "
        "and open it from there."
    );
    QMessageBox::information(nullptr, kAppName, msgBody.arg(kAppName));
    return 1;
  }

  if (!checkMacAssistiveDevices()) {
    return 1;
  }
#endif

  // --no-reset
  if (parser.isSet(resetOption)) {
    diagnostic::clearSettings(false);
  }

  MainWindow mainWindow;
  mainWindow.open();
  appendGuiDiagnosticLine(QStringLiteral("main window opened"));

  const auto exitCode = QApplication::exec();
  appendGuiDiagnosticLine(QStringLiteral("GUI event loop exited with code %1").arg(exitCode));
  return exitCode;
}

#if defined(Q_OS_MACOS)
bool checkMacAssistiveDevices()
{
  // new in mavericks, applications are trusted individually
  // with use of the accessibility api. this call will show a
  // prompt which can show the security/privacy/accessibility
  // tab, with a list of allowed applications. deskflow should
  // show up there automatically, but will be unchecked.

  if (AXIsProcessTrusted()) {
    return true;
  }

  const void *keys[] = {kAXTrustedCheckOptionPrompt};
  const void *trueValue[] = {kCFBooleanTrue};
  CFDictionaryRef options = CFDictionaryCreate(nullptr, keys, trueValue, 1, nullptr, nullptr);

  bool result = AXIsProcessTrustedWithOptions(options);
  CFRelease(options);
  return result;
}
#endif
