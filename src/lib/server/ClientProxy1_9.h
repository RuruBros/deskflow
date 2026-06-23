/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTransfer.h"
#include "server/ClientProxy1_8.h"

class EventQueueTimer;

class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_9() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard) override;
  void grabClipboard(ClipboardID id) override;
  bool parseMessage(const uint8_t *code) override;

private:
  void sendActions(std::vector<ClipboardTransferAction> actions);
  void handleOutputFlushed();
  void handleOutgoingTimeout();
  void handleIncomingTimeout();
  void armOutgoingTimer();
  void clearOutgoingTimer();
  void armIncomingTimer();
  void clearIncomingTimer();
  void extendIncomingHeartbeat();
  void restoreIncomingHeartbeat();

  bool recvClipboardTransfer();
  bool recvClipboardAck();
  bool recvClipboardCancel();
  void sendClipboardAck(uint32_t transferId);
  void sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason);

  IEventQueue *m_events;
  ClipboardTransferQueue m_outgoing;
  ClipboardTransferAssembler m_incoming;
  EventQueueTimer *m_outgoingTimer = nullptr;
  EventQueueTimer *m_incomingTimer = nullptr;
  bool m_incomingHeartbeatExtended = false;
  double m_savedHeartbeatAlarm = 0.0;
};
