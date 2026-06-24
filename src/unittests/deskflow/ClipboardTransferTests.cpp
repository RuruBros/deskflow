/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardTransfer.h"

#include <QTest>

class ClipboardTransferTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void sendsOneChunkPerFlush();
  void supersedesActiveClipboard();
  void remoteOwnershipSupersedesWithoutRetry();
  void ignoresAcknowledgedDuplicate();
  void queuesForcedAcknowledgedDuplicate();
  void ignoresAcknowledgmentBeforeEnd();
  void retriesThenDropsTimedOutTransfer();
  void replacesAndValidatesIncomingTransfer();
};

void ClipboardTransferTests::sendsOneChunkPerFlush()
{
  ClipboardTransferQueue queue;
  const std::string data(kClipboardTransferChunkSize + 5, 'x');

  auto actions = queue.queue(kClipboardClipboard, 7, data);
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Start);

  actions = queue.outputFlushed();
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Data);
  QCOMPARE(actions[0].data.size(), kClipboardTransferChunkSize);

  actions = queue.outputFlushed();
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Data);
  QCOMPARE(actions[0].data.size(), 5);

  actions = queue.outputFlushed();
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::End);

  const auto transferId = queue.activeTransferId();
  QVERIFY(transferId != 0);
  QVERIFY(queue.outputFlushed().empty());
  QVERIFY(queue.acknowledged(transferId).empty());
  QVERIFY(!queue.active());
}

void ClipboardTransferTests::supersedesActiveClipboard()
{
  ClipboardTransferQueue queue(0x80000000u);
  const auto first = queue.queue(kClipboardClipboard, 1, "first");
  const auto firstId = first[0].transferId;

  const auto actions = queue.queue(kClipboardClipboard, 2, "second");
  QCOMPARE(actions.size(), 2);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Cancel);
  QCOMPARE(actions[0].transferId, firstId);
  QCOMPARE(actions[0].cancelReason, ClipboardTransferCancelReason::Superseded);
  QCOMPARE(actions[1].type, ClipboardTransferActionType::Start);
  QVERIFY(actions[1].transferId != firstId);
  QVERIFY((actions[1].transferId & 0x80000000u) != 0);
}

void ClipboardTransferTests::remoteOwnershipSupersedesWithoutRetry()
{
  ClipboardTransferQueue queue(0x80000000u);
  auto actions = queue.queue(kClipboardClipboard, 1, "first");
  const auto transferId = actions[0].transferId;

  actions = queue.cancelled(transferId, ClipboardTransferCancelReason::Superseded);
  QVERIFY(actions.empty());
  QVERIFY(!queue.active());

  actions = queue.queue(kClipboardClipboard, 2, "second");
  QVERIFY(queue.active());
  actions = queue.supersede(kClipboardClipboard);
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Cancel);
  QCOMPARE(actions[0].cancelReason, ClipboardTransferCancelReason::Superseded);
  QVERIFY(!queue.active());
}

void ClipboardTransferTests::ignoresAcknowledgedDuplicate()
{
  ClipboardTransferQueue queue;
  auto actions = queue.queue(kClipboardClipboard, 1, "same");
  const auto transferId = actions[0].transferId;
  queue.outputFlushed();
  queue.outputFlushed();
  queue.outputFlushed();
  queue.acknowledged(transferId);

  QVERIFY(queue.queue(kClipboardClipboard, 2, "same").empty());
}

void ClipboardTransferTests::queuesForcedAcknowledgedDuplicate()
{
  ClipboardTransferQueue queue;
  auto actions = queue.queue(kClipboardClipboard, 1, "same");
  const auto transferId = actions[0].transferId;
  queue.outputFlushed();
  queue.outputFlushed();
  queue.acknowledged(transferId);

  actions = queue.queue(kClipboardClipboard, 2, "same", true);
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Start);
  QVERIFY(actions[0].transferId != transferId);
}

void ClipboardTransferTests::ignoresAcknowledgmentBeforeEnd()
{
  ClipboardTransferQueue queue;
  auto actions = queue.queue(kClipboardClipboard, 1, "data");
  const auto transferId = actions[0].transferId;

  QVERIFY(queue.acknowledged(transferId).empty());
  QVERIFY(queue.active());
}

void ClipboardTransferTests::retriesThenDropsTimedOutTransfer()
{
  ClipboardTransferQueue queue;
  auto actions = queue.queue(kClipboardClipboard, 1, "retry");
  auto transferId = actions[0].transferId;

  for (int retry = 0; retry < kClipboardTransferMaxRetries; ++retry) {
    actions = queue.timedOut();
    QCOMPARE(actions.size(), 2);
    QCOMPARE(actions[0].type, ClipboardTransferActionType::Cancel);
    QCOMPARE(actions[0].transferId, transferId);
    QCOMPARE(actions[1].type, ClipboardTransferActionType::Start);
    transferId = actions[1].transferId;
  }

  actions = queue.timedOut();
  QCOMPARE(actions.size(), 1);
  QCOMPARE(actions[0].type, ClipboardTransferActionType::Cancel);
  QVERIFY(!queue.active());
}

void ClipboardTransferTests::replacesAndValidatesIncomingTransfer()
{
  ClipboardTransferAssembler assembler;
  auto result = assembler.process(kClipboardClipboard, 1, 10, ChunkType::DataStart, "5");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::Started);

  result = assembler.process(kClipboardClipboard, 2, 11, ChunkType::DataStart, "3");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::Started);
  QCOMPARE(result.replacedTransferId, 10);

  result = assembler.process(kClipboardClipboard, 2, 11, ChunkType::DataChunk, "new");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::InProgress);
  result = assembler.process(kClipboardClipboard, 2, 11, ChunkType::DataEnd, "");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::Finished);
  QCOMPARE(assembler.data(), std::string("new"));

  assembler.reset();
  result = assembler.process(kClipboardClipboard, 3, 12, ChunkType::DataStart, "2");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::Started);
  result = assembler.process(kClipboardClipboard, 3, 12, ChunkType::DataChunk, "too long");
  QCOMPARE(result.status, ClipboardTransferReceiveStatus::Error);
  QVERIFY(!assembler.active());
}

QTEST_MAIN(ClipboardTransferTests)

#include "ClipboardTransferTests.moc"
