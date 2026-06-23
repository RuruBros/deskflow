/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardTransfer.h"

#include <algorithm>
#include <charconv>
#include <iterator>

ClipboardTransferQueue::ClipboardTransferQueue(uint32_t transferIdMask) : m_transferIdMask(transferIdMask & 0x80000000u)
{
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::queue(ClipboardID id, uint32_t sequence, std::string data)
{
  if (id >= kClipboardEnd) {
    return {};
  }

  if (m_active && m_active->clipboardId == id && m_active->data == data) {
    return {};
  }
  if (m_pending[id] && m_pending[id]->data == data) {
    return {};
  }
  if (!m_active && !m_pending[id] && m_hasAcknowledged[id] && m_lastAcknowledged[id] == data) {
    return {};
  }

  std::vector<ClipboardTransferAction> actions;
  if (m_active && m_active->clipboardId == id) {
    actions.push_back(
        {ClipboardTransferActionType::Cancel,
         id,
         m_active->sequence,
         m_active->transferId,
         ClipboardTransferCancelReason::Superseded,
         {}}
    );
    m_active.reset();
  }

  m_pending[id] = PendingTransfer{id, sequence, std::move(data), 0};
  if (!m_active) {
    auto start = startNext();
    actions.insert(actions.end(), std::make_move_iterator(start.begin()), std::make_move_iterator(start.end()));
  }
  return actions;
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::outputFlushed()
{
  if (!m_active) {
    return startNext();
  }

  auto &transfer = *m_active;
  if (transfer.phase == Phase::StartPendingFlush || transfer.phase == Phase::DataPendingFlush) {
    if (transfer.offset < transfer.data.size()) {
      const auto size = std::min(kClipboardTransferChunkSize, transfer.data.size() - transfer.offset);
      ClipboardTransferAction action{
          ClipboardTransferActionType::Data,
          transfer.clipboardId,
          transfer.sequence,
          transfer.transferId,
          ClipboardTransferCancelReason::Invalid,
          transfer.data.substr(transfer.offset, size)
      };
      transfer.offset += size;
      transfer.phase = Phase::DataPendingFlush;
      return {std::move(action)};
    }

    transfer.phase = Phase::EndPendingFlush;
    return {
        {ClipboardTransferActionType::End,
         transfer.clipboardId,
         transfer.sequence,
         transfer.transferId,
         ClipboardTransferCancelReason::Invalid,
         {}}
    };
  }

  if (transfer.phase == Phase::EndPendingFlush) {
    transfer.phase = Phase::AwaitingAck;
  }
  return {};
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::acknowledged(uint32_t transferId)
{
  if (!m_active || m_active->transferId != transferId ||
      (m_active->phase != Phase::EndPendingFlush && m_active->phase != Phase::AwaitingAck)) {
    return {};
  }

  m_lastAcknowledged[m_active->clipboardId] = m_active->data;
  m_hasAcknowledged[m_active->clipboardId] = true;
  m_active.reset();
  return startNext();
}

std::vector<ClipboardTransferAction>
ClipboardTransferQueue::cancelled(uint32_t transferId, ClipboardTransferCancelReason reason)
{
  if (!m_active || m_active->transferId != transferId) {
    return {};
  }
  if (reason == ClipboardTransferCancelReason::Superseded) {
    m_active.reset();
    return startNext();
  }
  return retryActive(reason);
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::supersede(ClipboardID id)
{
  if (id >= kClipboardEnd) {
    return {};
  }

  m_pending[id].reset();
  if (!m_active || m_active->clipboardId != id) {
    return {};
  }

  ClipboardTransferAction cancel{
      ClipboardTransferActionType::Cancel,
      m_active->clipboardId,
      m_active->sequence,
      m_active->transferId,
      ClipboardTransferCancelReason::Superseded,
      {}
  };
  m_active.reset();
  auto actions = std::vector<ClipboardTransferAction>{std::move(cancel)};
  auto start = startNext();
  actions.insert(actions.end(), std::make_move_iterator(start.begin()), std::make_move_iterator(start.end()));
  return actions;
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::timedOut()
{
  if (!m_active) {
    return {};
  }
  return retryActive(ClipboardTransferCancelReason::Timeout);
}

bool ClipboardTransferQueue::active() const
{
  return m_active.has_value();
}

uint32_t ClipboardTransferQueue::activeTransferId() const
{
  return m_active ? m_active->transferId : 0;
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::startNext()
{
  for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
    if (!m_pending[id]) {
      continue;
    }

    auto pending = std::move(*m_pending[id]);
    m_pending[id].reset();
    const auto transferId = nextTransferId();
    m_active = ActiveTransfer{pending.clipboardId,     pending.sequence, std::move(pending.data),
                              pending.retryCount,      transferId,       0,
                              Phase::StartPendingFlush};
    return {
        {ClipboardTransferActionType::Start, m_active->clipboardId, m_active->sequence, transferId,
         ClipboardTransferCancelReason::Invalid, std::to_string(m_active->data.size())}
    };
  }
  return {};
}

std::vector<ClipboardTransferAction> ClipboardTransferQueue::retryActive(ClipboardTransferCancelReason reason)
{
  auto transfer = std::move(*m_active);
  m_active.reset();

  std::vector<ClipboardTransferAction> actions{
      {ClipboardTransferActionType::Cancel, transfer.clipboardId, transfer.sequence, transfer.transferId, reason, {}}
  };

  if (transfer.retryCount < kClipboardTransferMaxRetries) {
    ++transfer.retryCount;
    m_pending[transfer.clipboardId] =
        PendingTransfer{transfer.clipboardId, transfer.sequence, std::move(transfer.data), transfer.retryCount};
  }

  auto start = startNext();
  actions.insert(actions.end(), std::make_move_iterator(start.begin()), std::make_move_iterator(start.end()));
  return actions;
}

uint32_t ClipboardTransferQueue::nextTransferId()
{
  const auto result = m_transferIdMask | m_nextTransferId++;
  if (m_nextTransferId == 0 || m_nextTransferId > 0x7fffffffu) {
    m_nextTransferId = 1;
  }
  return result;
}

ClipboardTransferReceiveResult ClipboardTransferAssembler::process(
    ClipboardID id, uint32_t sequence, uint32_t transferId, uint8_t mark, const std::string &data
)
{
  ClipboardTransferReceiveResult result{ClipboardTransferReceiveStatus::Ignored, 0, transferId, id, sequence};

  if (id >= kClipboardEnd || transferId == 0) {
    result.status = ClipboardTransferReceiveStatus::Error;
    return result;
  }

  if (mark == ChunkType::DataStart) {
    size_t expectedSize = 0;
    const auto *begin = data.data();
    const auto *end = begin + data.size();
    const auto parsed = std::from_chars(begin, end, expectedSize);
    if (parsed.ec != std::errc{} || parsed.ptr != end) {
      result.status = ClipboardTransferReceiveStatus::Error;
      return result;
    }

    if (m_active && m_transferId != transferId) {
      result.replacedTransferId = m_transferId;
    }
    m_active = true;
    m_clipboardId = id;
    m_sequence = sequence;
    m_transferId = transferId;
    m_expectedSize = expectedSize;
    m_data.clear();
    result.status = ClipboardTransferReceiveStatus::Started;
    return result;
  }

  if (!m_active || m_transferId != transferId || m_clipboardId != id || m_sequence != sequence) {
    result.status = ClipboardTransferReceiveStatus::Error;
    return result;
  }

  if (mark == ChunkType::DataChunk) {
    if (data.size() > m_expectedSize - std::min(m_expectedSize, m_data.size())) {
      reset();
      result.status = ClipboardTransferReceiveStatus::Error;
      return result;
    }
    m_data.append(data);
    result.status = ClipboardTransferReceiveStatus::InProgress;
    return result;
  }

  if (mark == ChunkType::DataEnd) {
    if (m_data.size() != m_expectedSize) {
      reset();
      result.status = ClipboardTransferReceiveStatus::Error;
      return result;
    }
    result.status = ClipboardTransferReceiveStatus::Finished;
    return result;
  }

  result.status = ClipboardTransferReceiveStatus::Error;
  return result;
}

void ClipboardTransferAssembler::cancel(uint32_t transferId)
{
  if (m_active && m_transferId == transferId) {
    reset();
  }
}

void ClipboardTransferAssembler::reset()
{
  m_active = false;
  m_clipboardId = kClipboardClipboard;
  m_sequence = 0;
  m_transferId = 0;
  m_expectedSize = 0;
  m_data.clear();
}

bool ClipboardTransferAssembler::active() const
{
  return m_active;
}

uint32_t ClipboardTransferAssembler::transferId() const
{
  return m_transferId;
}

ClipboardID ClipboardTransferAssembler::clipboardId() const
{
  return m_clipboardId;
}

const std::string &ClipboardTransferAssembler::data() const
{
  return m_data;
}
