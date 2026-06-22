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

std::string makeMacOsBitmapV5Dib()
{
  BITMAPV5HEADER header = {};
  header.bV5Size = sizeof(BITMAPV5HEADER);
  header.bV5Width = 2;
  header.bV5Height = 2;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5SizeImage = 16;
  header.bV5RedMask = 0x00ff0000;
  header.bV5GreenMask = 0x0000ff00;
  header.bV5BlueMask = 0x000000ff;
  header.bV5AlphaMask = 0xff000000;
  header.bV5CSType = LCS_sRGB;

  std::string dib(reinterpret_cast<const char *>(&header), sizeof(header));
  const char pixels[16] = {
      static_cast<char>(0x10), static_cast<char>(0x20), static_cast<char>(0x30), static_cast<char>(0x40),
      static_cast<char>(0x50), static_cast<char>(0x60), static_cast<char>(0x70), static_cast<char>(0x80),
      static_cast<char>(0x90), static_cast<char>(0xa0), static_cast<char>(0xb0), static_cast<char>(0xc0),
      static_cast<char>(0xd0), static_cast<char>(0xe0), static_cast<char>(0xf0), static_cast<char>(0xff),
  };
  dib.append(pixels, sizeof(pixels));
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

void MSWindowsClipboardTests::bitmapConverter_normalizesMacOsBitmapV5DibFromIClipboard()
{
  MSWindowsClipboardBitmapConverter converter;
  const auto source = makeMacOsBitmapV5Dib();

  HGLOBAL handle = converter.fromIClipboard(source);
  QVERIFY(handle != nullptr);
  QCOMPARE(GlobalSize(handle), sizeof(BITMAPINFOHEADER) + 16);

  const auto *data = static_cast<const char *>(GlobalLock(handle));
  QVERIFY(data != nullptr);
  const auto *header = reinterpret_cast<const BITMAPINFOHEADER *>(data);
  QCOMPARE(header->biSize, static_cast<DWORD>(sizeof(BITMAPINFOHEADER)));
  QCOMPARE(header->biWidth, static_cast<LONG>(2));
  QCOMPARE(header->biHeight, static_cast<LONG>(2));
  QCOMPARE(header->biPlanes, static_cast<WORD>(1));
  QCOMPARE(header->biBitCount, static_cast<WORD>(32));
  QCOMPARE(header->biCompression, static_cast<DWORD>(BI_RGB));
  QCOMPARE(header->biSizeImage, static_cast<DWORD>(16));
  QCOMPARE(
      std::string(data + sizeof(BITMAPINFOHEADER), 16),
      source.substr(sizeof(BITMAPV5HEADER), 16)
  );
  GlobalUnlock(handle);
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
