/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsClipboardTests.h"

#include "platform/MSWindowsClipboard.h"
#include "platform/MSWindowsClipboardBitmapConverter.h"

namespace {
std::string makeDib(size_t pixelBytes)
{
  BITMAPINFOHEADER header = {};
  header.biSize = sizeof(BITMAPINFOHEADER);
  header.biWidth = 2;
  header.biHeight = 2;
  header.biPlanes = 1;
  header.biBitCount = 32;
  header.biCompression = BI_RGB;

  std::string dib(reinterpret_cast<const char *>(&header), sizeof(header));
  dib.append(pixelBytes, '\0');
  return dib;
}

HGLOBAL makeGlobalDib(const std::string &dib)
{
  HGLOBAL handle = GlobalAlloc(GMEM_MOVEABLE, dib.size());
  if (handle == nullptr) {
    return nullptr;
  }

  auto *data = static_cast<char *>(GlobalLock(handle));
  if (data == nullptr) {
    GlobalFree(handle);
    return nullptr;
  }

  memcpy(data, dib.data(), dib.size());
  GlobalUnlock(handle);
  return handle;
}
} // namespace

void MSWindowsClipboardTests::initTestCase()
{
  m_log.setFilter(LogLevel::Level::Verbose);

  MSWindowsClipboard clipboard(NULL);

  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::cleanupTestCase()
{
  initTestCase();
}

void MSWindowsClipboardTests::emptyUnusedClipboard()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.emptyUnowned());
}

void MSWindowsClipboardTests::emptyOpenCalled()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
}

void MSWindowsClipboardTests::emptySingleFormat()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.empty());
  QVERIFY(!clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::addValue()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::replaceValue()
{
  using enum IClipboard::Format;

  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));

  clipboard.add(Text, m_testString);
  clipboard.add(Text, m_testString2);

  QCOMPARE(clipboard.get(Text), m_testString2);
}

void MSWindowsClipboardTests::openTimeIsOne()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
}

void MSWindowsClipboardTests::closeIsOpen()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  clipboard.close();
}

void MSWindowsClipboardTests::getTimeOpenWithNoEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  // this behavior is different to that of Clipboard which only
  // returns the value passed into open(t) after empty() is called.
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::getTimeOpenAndEmpty()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(1));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.getTime(), 1);
}

void MSWindowsClipboardTests::has_withFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QVERIFY(clipboard.has(IClipboard::Format::Text));
}

void MSWindowsClipboardTests::has_withNoFormatAdded()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  QCOMPARE(clipboard.get(IClipboard::Format::Text), "");
}

void MSWindowsClipboardTests::getNonEmptyText()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());

  clipboard.add(IClipboard::Format::Text, m_testString);
  QCOMPARE(clipboard.get(IClipboard::Format::Text), m_testString);
}

void MSWindowsClipboardTests::isOwnedByDeskflow()
{
  MSWindowsClipboard clipboard(NULL);
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.isOwnedByDeskflow());
}

void MSWindowsClipboardTests::bitmapConverter_rejectsTruncatedDibFromIClipboard()
{
  MSWindowsClipboardBitmapConverter converter;
  const auto truncatedDib = makeDib(0);

  QCOMPARE(converter.fromIClipboard(truncatedDib), nullptr);
}

void MSWindowsClipboardTests::bitmapConverter_rejectsNonCanonicalDibFromIClipboard()
{
  MSWindowsClipboardBitmapConverter converter;
  auto dib = makeDib(16);
  auto *header = reinterpret_cast<BITMAPINFOHEADER *>(dib.data());
  header->biSizeImage = 16;
  dib.push_back('\0');

  QCOMPARE(converter.fromIClipboard(dib), nullptr);
}

void MSWindowsClipboardTests::bitmapConverter_acceptsCompleteDibFromIClipboard()
{
  MSWindowsClipboardBitmapConverter converter;
  const auto completeDib = makeDib(16);

  HGLOBAL handle = converter.fromIClipboard(completeDib);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), completeDib.size());
  GlobalFree(handle);
}

void MSWindowsClipboardTests::bitmapConverter_rejectsTruncatedDibToIClipboard()
{
  MSWindowsClipboardBitmapConverter converter;
  const auto truncatedDib = makeDib(0);
  HGLOBAL handle = makeGlobalDib(truncatedDib);
  QVERIFY(handle != nullptr);

  QCOMPARE(converter.toIClipboard(handle), std::string());
  GlobalFree(handle);
}

QTEST_MAIN(MSWindowsClipboardTests)
