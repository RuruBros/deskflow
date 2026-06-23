/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <cstring>

namespace {
constexpr double kIncomingHeartbeatAlarm = kClipboardTransferInactivityTimeout + 5.0;
}

ClientProxy1_9::ClientProxy1_9(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_8(name, stream, server, events),
      m_events(events)
{
  m_events->addHandler(EventTypes::StreamOutputFlushed, getStream()->getEventTarget(), [this](const auto &) {
    handleOutputFlushed();
  });
}

ClientProxy1_9::~ClientProxy1_9()
{
  clearOutgoingTimer();
  clearIncomingTimer();
  m_events->removeHandler(EventTypes::StreamOutputFlushed, getStream()->getEventTarget());
}

void ClientProxy1_9::setClipboard(ClipboardID id, const IClipboard *clipboard)
{
  if (id >= kClipboardEnd || !m_clipboard[id].m_dirty) {
    return;
  }

  m_clipboard[id].m_dirty = false;
  Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);
  sendActions(m_outgoing.queue(id, 0, m_clipboard[id].m_clipboard.marshall()));
}

void ClientProxy1_9::grabClipboard(ClipboardID id)
{
  sendActions(m_outgoing.supersede(id));

  if (m_incoming.active() && m_incoming.clipboardId() == id) {
    const auto transferId = m_incoming.transferId();
    LOG_DEBUG(
        "cancelling clipboard transfer %u from \"%s\" because clipboard %u has a new owner", transferId,
        getName().c_str(), id
    );
    m_incoming.reset();
    clearIncomingTimer();
    restoreIncomingHeartbeat();
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Superseded);
  }
  ClientProxy1_8::grabClipboard(id);
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDClipboardTransfer, 4) == 0) {
    return recvClipboardTransfer();
  }
  if (memcmp(code, kMsgCClipboardAck, 4) == 0) {
    return recvClipboardAck();
  }
  if (memcmp(code, kMsgCClipboardCancel, 4) == 0) {
    return recvClipboardCancel();
  }
  return ClientProxy1_8::parseMessage(code);
}

void ClientProxy1_9::sendActions(std::vector<ClipboardTransferAction> actions)
{
  for (auto &action : actions) {
    switch (action.type) {
    case ClipboardTransferActionType::Start:
    case ClipboardTransferActionType::Data:
    case ClipboardTransferActionType::End: {
      const uint8_t mark = action.type == ClipboardTransferActionType::Start  ? ChunkType::DataStart
                           : action.type == ClipboardTransferActionType::Data ? ChunkType::DataChunk
                                                                              : ChunkType::DataEnd;
      ProtocolUtil::writef(
          getStream(), kMsgDClipboardTransfer, action.clipboardId, action.sequence, action.transferId, mark,
          &action.data
      );
      if (action.type == ClipboardTransferActionType::Start) {
        LOG_DEBUG(
            "starting clipboard transfer %u to \"%s\", size=%s", action.transferId, getName().c_str(),
            action.data.c_str()
        );
      } else if (action.type == ClipboardTransferActionType::End) {
        LOG_DEBUG(
            "finished sending clipboard transfer %u to \"%s\"; waiting for acknowledgment", action.transferId,
            getName().c_str()
        );
      }
      armOutgoingTimer();
      break;
    }

    case ClipboardTransferActionType::Cancel:
      LOG_DEBUG(
          "cancelling clipboard transfer %u to \"%s\", reason=%u", action.transferId, getName().c_str(),
          static_cast<uint8_t>(action.cancelReason)
      );
      sendClipboardCancel(action.transferId, action.cancelReason);
      break;
    }
  }

  if (!m_outgoing.active()) {
    clearOutgoingTimer();
  }
}

void ClientProxy1_9::handleOutputFlushed()
{
  if (!m_outgoing.active()) {
    clearOutgoingTimer();
    return;
  }

  auto actions = m_outgoing.outputFlushed();
  if (actions.empty()) {
    armOutgoingTimer();
  } else {
    sendActions(std::move(actions));
  }
}

void ClientProxy1_9::handleOutgoingTimeout()
{
  clearOutgoingTimer();
  LOG_WARN("clipboard transfer %u timed out while sending to \"%s\"", m_outgoing.activeTransferId(), getName().c_str());
  sendActions(m_outgoing.timedOut());
}

void ClientProxy1_9::handleIncomingTimeout()
{
  clearIncomingTimer();
  if (m_incoming.active()) {
    const auto transferId = m_incoming.transferId();
    LOG_WARN("clipboard transfer %u from \"%s\" timed out", transferId, getName().c_str());
    m_incoming.reset();
    restoreIncomingHeartbeat();
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Timeout);
  }
}

void ClientProxy1_9::armOutgoingTimer()
{
  clearOutgoingTimer();
  m_outgoingTimer = m_events->newOneShotTimer(kClipboardTransferInactivityTimeout, nullptr);
  m_events->addHandler(EventTypes::Timer, m_outgoingTimer, [this](const auto &) { handleOutgoingTimeout(); });
}

void ClientProxy1_9::clearOutgoingTimer()
{
  if (m_outgoingTimer != nullptr) {
    auto *timer = m_outgoingTimer;
    m_outgoingTimer = nullptr;
    m_events->removeHandler(EventTypes::Timer, timer);
    m_events->deleteTimer(timer);
  }
}

void ClientProxy1_9::armIncomingTimer()
{
  clearIncomingTimer();
  m_incomingTimer = m_events->newOneShotTimer(kClipboardTransferInactivityTimeout, nullptr);
  m_events->addHandler(EventTypes::Timer, m_incomingTimer, [this](const auto &) { handleIncomingTimeout(); });
}

void ClientProxy1_9::clearIncomingTimer()
{
  if (m_incomingTimer != nullptr) {
    auto *timer = m_incomingTimer;
    m_incomingTimer = nullptr;
    m_events->removeHandler(EventTypes::Timer, timer);
    m_events->deleteTimer(timer);
  }
}

void ClientProxy1_9::extendIncomingHeartbeat()
{
  if (m_incomingHeartbeatExtended) {
    setHeartbeatAlarm(kIncomingHeartbeatAlarm);
    return;
  }

  m_savedHeartbeatAlarm = heartbeatAlarm();
  m_incomingHeartbeatExtended = true;
  setHeartbeatAlarm(kIncomingHeartbeatAlarm);
}

void ClientProxy1_9::restoreIncomingHeartbeat()
{
  if (!m_incomingHeartbeatExtended) {
    return;
  }
  setHeartbeatAlarm(m_savedHeartbeatAlarm);
  m_savedHeartbeatAlarm = 0.0;
  m_incomingHeartbeatExtended = false;
}

bool ClientProxy1_9::recvClipboardTransfer()
{
  ClipboardID id;
  uint32_t sequence;
  uint32_t transferId;
  uint8_t mark;
  std::string data;
  if (!ProtocolUtil::readf(getStream(), kMsgDClipboardTransfer + 4, &id, &sequence, &transferId, &mark, &data)) {
    return false;
  }

  const bool wasCurrent = m_incoming.active() && m_incoming.transferId() == transferId;
  auto result = m_incoming.process(id, sequence, transferId, mark, data);
  if (result.replacedTransferId != 0) {
    sendClipboardCancel(result.replacedTransferId, ClipboardTransferCancelReason::Superseded);
  }

  switch (result.status) {
  case ClipboardTransferReceiveStatus::Started:
    LOG_DEBUG("receiving clipboard transfer %u from \"%s\", size=%s", transferId, getName().c_str(), data.c_str());
    extendIncomingHeartbeat();
    armIncomingTimer();
    return true;

  case ClipboardTransferReceiveStatus::InProgress:
    extendIncomingHeartbeat();
    armIncomingTimer();
    return true;

  case ClipboardTransferReceiveStatus::Finished: {
    clearIncomingTimer();
    restoreIncomingHeartbeat();
    try {
      m_clipboard[id].m_clipboard.unmarshall(m_incoming.data(), 0);
    } catch (...) {
      m_incoming.reset();
      sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
      return true;
    }
    m_clipboard[id].m_sequenceNumber = sequence;

    auto *info = new ClipboardInfo;
    info->m_id = id;
    info->m_sequenceNumber = sequence;
    m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
    m_incoming.reset();
    sendClipboardAck(transferId);
    LOG_INFO("clipboard transfer %u from \"%s\" completed", transferId, getName().c_str());
    return true;
  }

  case ClipboardTransferReceiveStatus::Error:
    if (wasCurrent || !m_incoming.active()) {
      clearIncomingTimer();
      restoreIncomingHeartbeat();
      m_incoming.reset();
    }
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
    return true;

  case ClipboardTransferReceiveStatus::Ignored:
    return true;
  }
  return true;
}

bool ClientProxy1_9::recvClipboardAck()
{
  uint32_t transferId;
  if (!ProtocolUtil::readf(getStream(), kMsgCClipboardAck + 4, &transferId)) {
    return false;
  }
  if (m_outgoing.activeTransferId() == transferId) {
    LOG_DEBUG("clipboard transfer %u to \"%s\" was acknowledged", transferId, getName().c_str());
    clearOutgoingTimer();
    sendActions(m_outgoing.acknowledged(transferId));
  }
  return true;
}

bool ClientProxy1_9::recvClipboardCancel()
{
  uint32_t transferId;
  uint8_t reason;
  if (!ProtocolUtil::readf(getStream(), kMsgCClipboardCancel + 4, &transferId, &reason)) {
    return false;
  }

  if (m_incoming.active() && m_incoming.transferId() == transferId) {
    LOG_DEBUG("clipboard transfer %u from \"%s\" was cancelled, reason=%u", transferId, getName().c_str(), reason);
    m_incoming.cancel(transferId);
    clearIncomingTimer();
    restoreIncomingHeartbeat();
  }

  if (m_outgoing.activeTransferId() == transferId) {
    const auto cancelReason = reason >= static_cast<uint8_t>(ClipboardTransferCancelReason::Superseded) &&
                                      reason <= static_cast<uint8_t>(ClipboardTransferCancelReason::Invalid)
                                  ? static_cast<ClipboardTransferCancelReason>(reason)
                                  : ClipboardTransferCancelReason::Invalid;
    clearOutgoingTimer();
    sendActions(m_outgoing.cancelled(transferId, cancelReason));
  }
  return true;
}

void ClientProxy1_9::sendClipboardAck(uint32_t transferId)
{
  ProtocolUtil::writef(getStream(), kMsgCClipboardAck, transferId);
}

void ClientProxy1_9::sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason)
{
  ProtocolUtil::writef(getStream(), kMsgCClipboardCancel, transferId, static_cast<uint8_t>(reason));
}
