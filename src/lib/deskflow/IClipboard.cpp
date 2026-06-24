/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/IClipboard.h"

#include "base/Log.h"

#include <assert.h>
#include <vector>

//
// IClipboard
//

bool IClipboard::unmarshall(IClipboard *clipboard, const std::string_view &data, Time time)
{
  assert(clipboard != nullptr);

  const char *index = data.data();
  const char *const end = index + data.size();

  if (end - index < 4) {
    LOG_ERR("clipboard unmarshall: truncated header");
    return false;
  }

  const uint32_t numFormats = readUInt32(index);
  index += 4;
  if (numFormats == 0) {
    LOG_DEBUG("clipboard unmarshall: no formats");
    return false;
  }
  if (numFormats > static_cast<uint32_t>((end - index) / 8)) {
    LOG_ERR("clipboard unmarshall: format count %u exceeds payload size", numFormats);
    return false;
  }

  struct ClipboardFormatData
  {
    IClipboard::Format format;
    std::string_view data;
  };

  std::vector<ClipboardFormatData> formats;
  formats.reserve(numFormats);

  for (uint32_t i = 0; i < numFormats; ++i) {
    if (end - index < 8) {
      LOG_ERR("clipboard unmarshall: truncated format header at %u/%u", i, numFormats);
      return false;
    }

    auto format = static_cast<IClipboard::Format>(readUInt32(index));
    index += 4;

    const uint32_t size = readUInt32(index);
    index += 4;

    if (size > static_cast<uint32_t>(end - index)) {
      LOG_ERR("clipboard unmarshall: payload size %u exceeds remaining %zd", size, end - index);
      return false;
    }

    if (format < IClipboard::Format::TotalFormats) {
      formats.push_back({format, std::string_view(index, size)});
    }
    index += size;
  }

  if (index != end) {
    LOG_ERR("clipboard unmarshall: trailing data");
    return false;
  }

  if (formats.empty()) {
    LOG_DEBUG("clipboard unmarshall: no supported formats");
    return false;
  }

  if (clipboard->open(time)) {
    if (!clipboard->empty()) {
      clipboard->close();
      return false;
    }
    for (const auto &format : formats) {
      clipboard->add(format.format, std::string(format.data));
    }
    clipboard->close();
    return true;
  }

  return false;
}

std::string IClipboard::marshall(const IClipboard *clipboard)
{
  // return data format:
  // 4 bytes => number of formats included
  // 4 bytes => format enum
  // 4 bytes => clipboard data size n
  // n bytes => clipboard data
  // back to the second 4 bytes if there is another format

  assert(clipboard != nullptr);

  std::string data;
  static const auto totalClipboardFormats = static_cast<int>(Format::TotalFormats);
  std::vector<std::string> formatData;
  formatData.resize(totalClipboardFormats);
  // FIXME -- use current time
  if (clipboard->open(0)) {

    // compute size of marshalled data
    uint32_t size = 4;
    uint32_t numFormats = 0;
    for (uint32_t format = 0; format != totalClipboardFormats; ++format) {
      if (clipboard->has(static_cast<IClipboard::Format>(format))) {
        ++numFormats;
        formatData[format] = clipboard->get(static_cast<IClipboard::Format>(format));
        size += 4 + 4 + (uint32_t)formatData[format].size();
      }
    }

    // allocate space
    data.reserve(size);

    // marshall the data
    writeUInt32(&data, numFormats);
    for (uint32_t format = 0; format != totalClipboardFormats; ++format) {
      if (clipboard->has(static_cast<IClipboard::Format>(format))) {
        writeUInt32(&data, format);
        writeUInt32(&data, (uint32_t)formatData[format].size());
        data += formatData[format];
      }
    }
    clipboard->close();
  }

  return data;
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  return copy(dst, src, src->getTime());
}

bool IClipboard::copy(IClipboard *dst, const IClipboard *src, Time time)
{
  assert(dst != nullptr);
  assert(src != nullptr);

  bool success = false;
  if (src->open(time)) {
    if (dst->open(time)) {
      if (dst->empty()) {
        for (int32_t format = 0; format != static_cast<int>(Format::TotalFormats); ++format) {
          auto eFormat = (IClipboard::Format)format;
          if (src->has(eFormat)) {
            dst->add(eFormat, src->get(eFormat));
          }
        }
        success = true;
      }
      dst->close();
    }
    src->close();
  }

  return success;
}

uint32_t IClipboard::readUInt32(const char *buf)
{
  const auto *ubuf = reinterpret_cast<const unsigned char *>(buf);
  return (static_cast<uint32_t>(ubuf[0]) << 24) | (static_cast<uint32_t>(ubuf[1]) << 16) |
         (static_cast<uint32_t>(ubuf[2]) << 8) | static_cast<uint32_t>(ubuf[3]);
}

void IClipboard::writeUInt32(std::string *buf, uint32_t v)
{
  *buf += static_cast<uint8_t>((v >> 24) & 0xff);
  *buf += static_cast<uint8_t>((v >> 16) & 0xff);
  *buf += static_cast<uint8_t>((v >> 8) & 0xff);
  *buf += static_cast<uint8_t>(v & 0xff);
}
