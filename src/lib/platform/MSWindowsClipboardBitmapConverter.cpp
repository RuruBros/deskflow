/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardBitmapConverter.h"

#include "base/Log.h"

#include <limits>

namespace {
constexpr uint32_t kBiAlphabitfields = 6;

bool checkedAdd(size_t lhs, size_t rhs, size_t &result)
{
  if (lhs > std::numeric_limits<size_t>::max() - rhs) {
    return false;
  }
  result = lhs + rhs;
  return true;
}

bool checkedMul(size_t lhs, size_t rhs, size_t &result)
{
  if (lhs != 0 && rhs > std::numeric_limits<size_t>::max() / lhs) {
    return false;
  }
  result = lhs * rhs;
  return true;
}

bool getDibLayout(
    const void *data, size_t size, const BITMAPINFOHEADER *&bitmap, size_t &pixelOffset, size_t &pixelBytes
)
{
  if (data == nullptr || size < sizeof(BITMAPINFOHEADER)) {
    LOG_DEBUG("rejecting clipboard bitmap, DIB is too small: %zu bytes", size);
    return false;
  }

  bitmap = static_cast<const BITMAPINFOHEADER *>(data);
  if (bitmap->biSize < sizeof(BITMAPINFOHEADER) || bitmap->biSize > size) {
    LOG_DEBUG("rejecting clipboard bitmap, invalid DIB header size: %u", bitmap->biSize);
    return false;
  }
  if (bitmap->biPlanes != 1 || bitmap->biWidth <= 0 || bitmap->biHeight == 0 ||
      bitmap->biHeight == std::numeric_limits<LONG>::min() || (bitmap->biBitCount != 24 && bitmap->biBitCount != 32)) {
    LOG_DEBUG(
        "rejecting clipboard bitmap, unsupported DIB: %dx%d planes=%d depth=%d", bitmap->biWidth, bitmap->biHeight,
        bitmap->biPlanes, bitmap->biBitCount
    );
    return false;
  }
  if (bitmap->biCompression != BI_RGB && bitmap->biCompression != BI_BITFIELDS &&
      bitmap->biCompression != kBiAlphabitfields) {
    LOG_DEBUG("rejecting clipboard bitmap, unsupported compression: %u", bitmap->biCompression);
    return false;
  }

  pixelOffset = bitmap->biSize;
  if (bitmap->biSize == sizeof(BITMAPINFOHEADER)) {
    if (bitmap->biCompression == BI_BITFIELDS) {
      if (!checkedAdd(pixelOffset, 3 * sizeof(DWORD), pixelOffset)) {
        return false;
      }
    } else if (bitmap->biCompression == kBiAlphabitfields) {
      if (!checkedAdd(pixelOffset, 4 * sizeof(DWORD), pixelOffset)) {
        return false;
      }
    }
  }
  if (pixelOffset > size) {
    LOG_DEBUG("rejecting clipboard bitmap, pixel offset exceeds DIB size: %zu > %zu", pixelOffset, size);
    return false;
  }

  size_t rowBits = 0;
  if (!checkedMul(static_cast<size_t>(bitmap->biWidth), static_cast<size_t>(bitmap->biBitCount), rowBits)) {
    return false;
  }
  size_t rowBytes = 0;
  if (!checkedAdd(rowBits, 31, rowBytes)) {
    return false;
  }
  rowBytes = (rowBytes / 32) * 4;

  const auto height = bitmap->biHeight < 0 ? -static_cast<int64_t>(bitmap->biHeight) : bitmap->biHeight;
  if (!checkedMul(rowBytes, static_cast<size_t>(height), pixelBytes)) {
    return false;
  }

  size_t requiredSize = 0;
  if (!checkedAdd(pixelOffset, pixelBytes, requiredSize) || requiredSize > size) {
    LOG_DEBUG("rejecting clipboard bitmap, pixel data exceeds DIB size: %zu > %zu", requiredSize, size);
    return false;
  }

  return true;
}

bool isCanonicalIClipboardDib(const BITMAPINFOHEADER *bitmap, size_t pixelOffset, size_t pixelBytes, size_t size)
{
  size_t requiredSize = 0;
  if (!checkedAdd(pixelOffset, pixelBytes, requiredSize)) {
    return false;
  }

  if (requiredSize != size || (bitmap->biSizeImage != 0 && bitmap->biSizeImage != pixelBytes)) {
    return false;
  }

  return bitmap->biSize == sizeof(BITMAPINFOHEADER) && bitmap->biCompression == BI_RGB && bitmap->biClrUsed == 0 &&
         bitmap->biClrImportant == 0 && pixelOffset == sizeof(BITMAPINFOHEADER);
}

bool isCompleteDibPayload(const BITMAPINFOHEADER *bitmap, size_t pixelOffset, size_t pixelBytes, size_t size)
{
  size_t requiredSize = 0;
  if (!checkedAdd(pixelOffset, pixelBytes, requiredSize) || requiredSize != size) {
    LOG_DEBUG("rejecting clipboard bitmap, inconsistent payload size: required=%zu size=%zu", requiredSize, size);
    return false;
  }

  if (bitmap->biSizeImage != 0 && bitmap->biSizeImage != pixelBytes) {
    LOG_DEBUG(
        "rejecting clipboard bitmap, inconsistent image size: header=%u actual=%zu", bitmap->biSizeImage, pixelBytes
    );
    return false;
  }

  return true;
}

std::string normalizeMacOsBitmapV5(
    const void *data, const BITMAPINFOHEADER *bitmap, size_t pixelOffset, size_t pixelBytes
)
{
  if (bitmap->biSize != sizeof(BITMAPV5HEADER) || bitmap->biBitCount != 32 ||
      bitmap->biCompression != BI_BITFIELDS || pixelOffset != sizeof(BITMAPV5HEADER) ||
      pixelBytes > std::numeric_limits<DWORD>::max()) {
    return {};
  }

  BITMAPV5HEADER source = {};
  memcpy(&source, data, sizeof(source));
  if (source.bV5RedMask != 0x00ff0000 || source.bV5GreenMask != 0x0000ff00 ||
      source.bV5BlueMask != 0x000000ff || (source.bV5AlphaMask != 0 && source.bV5AlphaMask != 0xff000000)) {
    LOG_DEBUG(
        "rejecting macOS clipboard bitmap, unsupported masks: red=%08x green=%08x blue=%08x alpha=%08x",
        source.bV5RedMask, source.bV5GreenMask, source.bV5BlueMask, source.bV5AlphaMask
    );
    return {};
  }

  BITMAPINFOHEADER normalized = {};
  normalized.biSize = sizeof(normalized);
  normalized.biWidth = source.bV5Width;
  normalized.biHeight = source.bV5Height;
  normalized.biPlanes = source.bV5Planes;
  normalized.biBitCount = source.bV5BitCount;
  normalized.biCompression = BI_RGB;
  normalized.biSizeImage = static_cast<DWORD>(pixelBytes);
  normalized.biXPelsPerMeter = source.bV5XPelsPerMeter;
  normalized.biYPelsPerMeter = source.bV5YPelsPerMeter;

  std::string result(reinterpret_cast<const char *>(&normalized), sizeof(normalized));
  result.append(static_cast<const char *>(data) + pixelOffset, pixelBytes);
  return result;
}

HGLOBAL copyToGlobalMemory(const void *data, size_t size)
{
  HGLOBAL result = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, size);
  if (result == nullptr) {
    return nullptr;
  }

  auto *destination = static_cast<char *>(GlobalLock(result));
  if (destination == nullptr) {
    GlobalFree(result);
    return nullptr;
  }

  memcpy(destination, data, size);
  GlobalUnlock(result);
  return result;
}
} // namespace

//
// MSWindowsClipboardBitmapConverter
//

IClipboard::Format MSWindowsClipboardBitmapConverter::getFormat() const
{
  return IClipboard::Format::Bitmap;
}

UINT MSWindowsClipboardBitmapConverter::getWin32Format() const
{
  return CF_DIB;
}

HANDLE
MSWindowsClipboardBitmapConverter::fromIClipboard(const std::string &data) const
{
  const BITMAPINFOHEADER *bitmap = nullptr;
  size_t pixelOffset = 0;
  size_t pixelBytes = 0;
  if (!getDibLayout(data.data(), data.size(), bitmap, pixelOffset, pixelBytes)) {
    return nullptr;
  }
  if (!isCompleteDibPayload(bitmap, pixelOffset, pixelBytes, data.size())) {
    return nullptr;
  }

  if (isCanonicalIClipboardDib(bitmap, pixelOffset, pixelBytes, data.size())) {
    return copyToGlobalMemory(data.data(), data.size());
  }

  const auto normalized = normalizeMacOsBitmapV5(data.data(), bitmap, pixelOffset, pixelBytes);
  if (!normalized.empty()) {
    LOG_DEBUG("normalized macOS BITMAPV5HEADER clipboard bitmap to BI_RGB");
    return copyToGlobalMemory(normalized.data(), normalized.size());
  }

  LOG_DEBUG(
      "rejecting clipboard bitmap, unsupported DIB layout: header=%u compression=%u offset=%zu size=%zu",
      bitmap->biSize, bitmap->biCompression, pixelOffset, data.size()
  );
  return nullptr;
}

std::string MSWindowsClipboardBitmapConverter::toIClipboard(HANDLE data) const
{
  // get data
  LPVOID src = GlobalLock(data);
  if (src == nullptr) {
    return std::string();
  }
  const size_t srcSize = GlobalSize(data);

  const BITMAPINFOHEADER *bitmap = nullptr;
  size_t srcBitsOffset = 0;
  size_t srcPixelBytes = 0;
  if (!getDibLayout(src, srcSize, bitmap, srcBitsOffset, srcPixelBytes)) {
    GlobalUnlock(data);
    return std::string();
  }
  (void)srcPixelBytes;

  // check image type
  LOG((CLOG_INFO "bitmap: %dx%d %d", bitmap->biWidth, bitmap->biHeight, static_cast<int>(bitmap->biBitCount)));
  if (bitmap->biCompression == BI_RGB) {
    // already in canonical form
    std::string image(static_cast<char const *>(src), srcSize);
    GlobalUnlock(data);
    return image;
  }

  // create a destination DIB section
  LOG_INFO("convert image from: depth=%d comp=%d", bitmap->biBitCount, bitmap->biCompression);
  void *raw;
  BITMAPINFOHEADER info;
  LONG w = bitmap->biWidth;
  LONG h = bitmap->biHeight < 0 ? -bitmap->biHeight : bitmap->biHeight;
  info.biSize = sizeof(BITMAPINFOHEADER);
  info.biWidth = w;
  info.biHeight = h;
  info.biPlanes = 1;
  info.biBitCount = 32;
  info.biCompression = BI_RGB;
  info.biSizeImage = 0;
  info.biXPelsPerMeter = 1000;
  info.biYPelsPerMeter = 1000;
  info.biClrUsed = 0;
  info.biClrImportant = 0;
  HDC dc = GetDC(nullptr);
  HBITMAP dst = CreateDIBSection(dc, (BITMAPINFO *)&info, DIB_RGB_COLORS, &raw, nullptr, 0);
  if (dst == nullptr || raw == nullptr) {
    if (dst != nullptr) {
      DeleteObject(dst);
    }
    ReleaseDC(nullptr, dc);
    GlobalUnlock(data);
    return std::string();
  }

  // find the start of the pixel data
  const char *srcBits = static_cast<const char *>(src) + srcBitsOffset;

  // copy source image to destination image
  HDC dstDC = CreateCompatibleDC(dc);
  HGDIOBJ oldBitmap = SelectObject(dstDC, dst);
  SetDIBitsToDevice(
      dstDC, 0, 0, w, h, 0, 0, 0, h, srcBits, reinterpret_cast<const BITMAPINFO *>(bitmap), DIB_RGB_COLORS
  );
  SelectObject(dstDC, oldBitmap);
  DeleteDC(dstDC);
  GdiFlush();

  // extract data
  std::string image((const char *)&info, info.biSize);
  image.append(static_cast<const char *>(raw), 4 * static_cast<size_t>(w) * static_cast<size_t>(h));

  // clean up GDI
  DeleteObject(dst);
  ReleaseDC(nullptr, dc);

  // release handle
  GlobalUnlock(data);

  return image;
}
