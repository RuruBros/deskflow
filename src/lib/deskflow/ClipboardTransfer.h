/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"
#include "deskflow/ProtocolTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

constexpr size_t kClipboardTransferChunkSize = 16 * 1024;
constexpr double kClipboardTransferInactivityTimeout = 10.0;
constexpr int kClipboardTransferMaxRetries = 2;

enum class ClipboardTransferActionType
{
  Start,
  Data,
  End,
  Cancel
};

struct ClipboardTransferAction
{
  ClipboardTransferActionType type;
  ClipboardID clipboardId = kClipboardClipboard;
  uint32_t sequence = 0;
  uint32_t transferId = 0;
  ClipboardTransferCancelReason cancelReason = ClipboardTransferCancelReason::Invalid;
  std::string data;
};

class ClipboardTransferQueue
{
public:
  explicit ClipboardTransferQueue(uint32_t transferIdMask = 0);

  std::vector<ClipboardTransferAction> queue(ClipboardID id, uint32_t sequence, std::string data);
  std::vector<ClipboardTransferAction> outputFlushed();
  std::vector<ClipboardTransferAction> acknowledged(uint32_t transferId);
  std::vector<ClipboardTransferAction> cancelled(uint32_t transferId, ClipboardTransferCancelReason reason);
  std::vector<ClipboardTransferAction> supersede(ClipboardID id);
  std::vector<ClipboardTransferAction> timedOut();

  bool active() const;
  uint32_t activeTransferId() const;

private:
  enum class Phase
  {
    StartPendingFlush,
    DataPendingFlush,
    EndPendingFlush,
    AwaitingAck
  };

  struct PendingTransfer
  {
    ClipboardID clipboardId;
    uint32_t sequence;
    std::string data;
    int retryCount = 0;
  };

  struct ActiveTransfer : PendingTransfer
  {
    uint32_t transferId;
    size_t offset = 0;
    Phase phase = Phase::StartPendingFlush;
  };

  std::vector<ClipboardTransferAction> startNext();
  std::vector<ClipboardTransferAction> retryActive(ClipboardTransferCancelReason reason);
  uint32_t nextTransferId();

  std::array<std::optional<PendingTransfer>, kClipboardEnd> m_pending;
  std::array<std::string, kClipboardEnd> m_lastAcknowledged;
  std::array<bool, kClipboardEnd> m_hasAcknowledged{};
  std::optional<ActiveTransfer> m_active;
  uint32_t m_transferIdMask = 0;
  uint32_t m_nextTransferId = 1;
};

enum class ClipboardTransferReceiveStatus
{
  Started,
  InProgress,
  Finished,
  Ignored,
  Error
};

struct ClipboardTransferReceiveResult
{
  ClipboardTransferReceiveStatus status = ClipboardTransferReceiveStatus::Ignored;
  uint32_t replacedTransferId = 0;
  uint32_t transferId = 0;
  ClipboardID clipboardId = kClipboardClipboard;
  uint32_t sequence = 0;
};

class ClipboardTransferAssembler
{
public:
  ClipboardTransferReceiveResult
  process(ClipboardID id, uint32_t sequence, uint32_t transferId, uint8_t mark, const std::string &data);
  void cancel(uint32_t transferId);
  void reset();

  bool active() const;
  uint32_t transferId() const;
  ClipboardID clipboardId() const;
  const std::string &data() const;

private:
  bool m_active = false;
  ClipboardID m_clipboardId = kClipboardClipboard;
  uint32_t m_sequence = 0;
  uint32_t m_transferId = 0;
  size_t m_expectedSize = 0;
  std::string m_data;
};
