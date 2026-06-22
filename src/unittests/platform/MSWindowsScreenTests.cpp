/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsScreenTests.h"

#include "platform/MSWindowsKeyInput.h"

void MSWindowsScreenTests::localKeyInputUsesNoFlagsForRegularKeyDown()
{
  const auto input = makeWindowsKeyInput(VK_SHIFT, 0x2au, true);

  QCOMPARE(input.type, static_cast<DWORD>(INPUT_KEYBOARD));
  QCOMPARE(input.ki.wVk, static_cast<WORD>(VK_SHIFT));
  QCOMPARE(input.ki.wScan, static_cast<WORD>(0));
  QCOMPARE(input.ki.dwFlags, static_cast<DWORD>(0));
  QCOMPARE(input.ki.time, static_cast<DWORD>(0));
  QCOMPARE(input.ki.dwExtraInfo, static_cast<ULONG_PTR>(0));
}

void MSWindowsScreenTests::localKeyInputUsesKeyUpForRegularKeyRelease()
{
  const auto input = makeWindowsKeyInput(VK_SHIFT, 0x2au, false);

  QCOMPARE(input.ki.dwFlags, static_cast<DWORD>(KEYEVENTF_KEYUP));
}

void MSWindowsScreenTests::localKeyInputPreservesExtendedFlagForKeyDown()
{
  const auto input = makeWindowsKeyInput(VK_LWIN, 0x15bu, true);

  QCOMPARE(input.ki.dwFlags, static_cast<DWORD>(KEYEVENTF_EXTENDEDKEY));
}

void MSWindowsScreenTests::localKeyInputPreservesExtendedFlagForKeyRelease()
{
  const auto input = makeWindowsKeyInput(VK_LWIN, 0x15bu, false);

  QCOMPARE(input.ki.dwFlags, static_cast<DWORD>(KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP));
}

QTEST_MAIN(MSWindowsScreenTests)
