/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTransfer.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/KeyboardLayoutManager.h"

class Client;
class ClientInfo;
class EventQueueTimer;
class IClipboard;
namespace deskflow {
class IStream;
}
class IEventQueue;

//! Proxy for server
/*!
This class acts a proxy for the server, converting calls into messages
to the server and messages from the server to calls on the client.
*/
class ServerProxy
{
public:
  /*!
  Process messages from the server on \p stream and forward to
  \p client.
  */
  ServerProxy(Client *client, deskflow::IStream *stream, IEventQueue *events, bool transactionalClipboard);
  ServerProxy(ServerProxy const &) = delete;
  ServerProxy(ServerProxy &&) = delete;
  ~ServerProxy();

  ServerProxy &operator=(ServerProxy const &) = delete;
  ServerProxy &operator=(ServerProxy &&) = delete;

  //! @name manipulators
  //@{

  void onInfoChanged();
  bool onGrabClipboard(ClipboardID);
  void onClipboardChanged(ClipboardID, const IClipboard *, bool force = false);

  //@}

protected:
  enum class ConnectionResult
  {
    Okay,
    Unknown,
    Disconnect
  };
  ConnectionResult parseHandshakeMessage(const uint8_t *code);
  ConnectionResult parseMessage(const uint8_t *code);

private:
  // if compressing mouse motion then send the last motion now
  void flushCompressedMouse();

  void sendInfo(const ClientInfo &);

  void resetKeepAliveAlarm();
  void setKeepAliveRate(double);
  void extendKeepAliveForClipboardTransfer(bool &extended);
  void restoreKeepAliveAfterClipboardTransfer(bool &extended);
  void extendKeepAliveForClipboardIncomingTransfer();
  void restoreKeepAliveAfterClipboardIncomingTransfer();
  void extendKeepAliveForClipboardOutgoingTransfer();
  void restoreKeepAliveAfterClipboardOutgoingTransfer();

  void sendClipboardActions(std::vector<ClipboardTransferAction> actions);
  void handleClipboardOutputFlushed();
  void handleClipboardOutgoingTimeout();
  void handleClipboardIncomingTimeout();
  void armClipboardOutgoingTimer();
  void clearClipboardOutgoingTimer();
  void armClipboardIncomingTimer();
  void clearClipboardIncomingTimer();

  // modifier key translation
  KeyID translateKey(KeyID) const;
  KeyModifierMask translateModifierMask(KeyModifierMask) const;

  // event handlers
  void handleData();
  void handleKeepAliveAlarm();

  // message handlers
  void enter();
  void leave();
  void setClipboard();
  void setClipboardTransfer();
  void acknowledgeClipboardTransfer();
  void cancelClipboardTransfer();
  void sendClipboardAck(uint32_t transferId);
  void sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason);
  void grabClipboard();
  void keyDown(uint16_t id, uint16_t mask, uint16_t button, const std::string &lang);
  void keyRepeat();
  void keyUp();
  void mouseDown();
  void mouseUp();
  void mouseMove();
  void mouseRelativeMove();
  void mouseWheel();
  void screensaver();
  void resetOptions();
  void setOptions();
  void queryInfo();
  void infoAcknowledgment();
  void secureInputNotification();
  void setServerLanguages();
  void setActiveServerLanguage(const std::string_view &language);

private:
  using MessageParser = ConnectionResult (ServerProxy::*)(const uint8_t *);

  Client *m_client = nullptr;
  deskflow::IStream *m_stream = nullptr;

  uint32_t m_seqNum = 0;

  bool m_compressMouse = false;
  bool m_compressMouseRelative = false;
  int32_t m_xMouse = 0;
  int32_t m_yMouse = 0;
  int32_t m_dxMouse = 0;
  int32_t m_dyMouse = 0;

  bool m_ignoreMouse = false;

  KeyModifierID m_modifierTranslationTable[kKeyModifierIDLast];

  double m_keepAliveAlarm = 0.0;
  EventQueueTimer *m_keepAliveAlarmTimer = nullptr;
  double m_savedKeepAliveAlarm = 0.0;
  bool m_clipboardIncomingKeepAliveExtended = false;
  bool m_clipboardOutgoingKeepAliveExtended = false;

  bool m_transactionalClipboard = false;
  ClipboardTransferQueue m_clipboardOutgoing{0x80000000u};
  ClipboardTransferAssembler m_clipboardIncoming;
  EventQueueTimer *m_clipboardOutgoingTimer = nullptr;
  EventQueueTimer *m_clipboardIncomingTimer = nullptr;

  MessageParser m_parser = &ServerProxy::parseHandshakeMessage;
  IEventQueue *m_events = nullptr;
  std::string m_serverLayout = "";
  bool m_isUserNotifiedAboutLayoutSyncError = false;
  deskflow::KeyboardLayoutManager m_layoutManager;
};
