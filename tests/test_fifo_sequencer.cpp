#include <gtest/gtest.h>

#include "common/logging.hpp"
#include "exchange/order_server/fifo_sequencer.hpp"

using namespace exchange;

class FIFOSequencerTest : public ::testing::Test {
protected:
  static ClientRequestImplQueue *queue;
  static common::Logger *logger;

  FIFOSequencer *sequencer = nullptr;

  static void SetUpTestSuite() {
    queue = new ClientRequestImplQueue(1024);
    logger = new common::Logger("test_fifo_sequencer.log");
  }

  static void TearDownTestSuite() {
    delete queue;
    queue = nullptr;
    delete logger;
    logger = nullptr;
  }

  void SetUp() override {
    while (queue->size())
      queue->updateReadIndex();
    sequencer = new FIFOSequencer(queue, logger);
  }

  void TearDown() override {
    delete sequencer;
    sequencer = nullptr;
    while (queue->size())
      queue->updateReadIndex();
  }

  ClientRequestImpl makeReq(ClientId c, OrderId o) {
    return ClientRequestImpl{ClientRequestType::NEW, c,   0, o,
                             common::Side::BUY,      100, 10};
  }
};

ClientRequestImplQueue *FIFOSequencerTest::queue = nullptr;
common::Logger *FIFOSequencerTest::logger = nullptr;

// Verifies publishing with no pending requests leaves the queue empty.
TEST_F(FIFOSequencerTest, EmptyPublishIsNoOp) {
  sequencer->sequenceAndPublish();
  EXPECT_EQ(queue->size(), 0u);
}

// Verifies a single request passes through unchanged.
TEST_F(FIFOSequencerTest, SingleRequestPassesThrough) {
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->sequenceAndPublish();
  ASSERT_EQ(queue->size(), 1u);
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 11u);
}

// Verifies out-of-order arrival times are sorted ascending by recv time.
TEST_F(FIFOSequencerTest, OutOfOrderRequestsAreTimeSorted) {
  sequencer->addClientRequest(300, makeReq(1, 13));
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->addClientRequest(200, makeReq(1, 12));
  sequencer->sequenceAndPublish();

  ASSERT_EQ(queue->size(), 3u);
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 11u);
  queue->updateReadIndex();
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 12u);
  queue->updateReadIndex();
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 13u);
}

// Verifies requests from different clients are merged into one time-ordered
// stream.
TEST_F(FIFOSequencerTest, MultipleClientsMergedByTime) {
  sequencer->addClientRequest(200, makeReq(2, 22));
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->sequenceAndPublish();

  ASSERT_EQ(queue->size(), 2u);
  EXPECT_EQ(queue->getNextToRead()->m_clientId, 1u);
  queue->updateReadIndex();
  EXPECT_EQ(queue->getNextToRead()->m_clientId, 2u);
}

// Verifies publish drains the pending buffer so a second publish is empty.
TEST_F(FIFOSequencerTest, PublishClearsPendingBuffer) {
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->sequenceAndPublish();
  EXPECT_EQ(queue->size(), 1u);
  queue->updateReadIndex();

  sequencer->sequenceAndPublish();
  EXPECT_EQ(queue->size(), 0u);
}

// Verifies already-sorted input stays in the same order.
TEST_F(FIFOSequencerTest, SortedInputOrderPreserved) {
  for (int i = 0; i < 5; ++i) {
    sequencer->addClientRequest(100 + i * 10, makeReq(1, 30 + i));
  }
  sequencer->sequenceAndPublish();

  ASSERT_EQ(queue->size(), 5u);
  for (int i = 0; i < 5; ++i) {
    EXPECT_EQ(queue->getNextToRead()->m_orderId, static_cast<OrderId>(30 + i));
    queue->updateReadIndex();
  }
}

// Verifies cancel-type requests are sequenced like new requests.
TEST_F(FIFOSequencerTest, CancelRequestsAreSequenced) {
  ClientRequestImpl cancel{ClientRequestType::CANCEL, 1, 0, 99,
                           common::Side::INVALID,     0, 0};
  sequencer->addClientRequest(50, cancel);
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->sequenceAndPublish();

  ASSERT_EQ(queue->size(), 2u);
  EXPECT_EQ(queue->getNextToRead()->m_type, ClientRequestType::CANCEL);
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 99u);
  queue->updateReadIndex();
  EXPECT_EQ(queue->getNextToRead()->m_type, ClientRequestType::NEW);
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 11u);
}

// Verifies a larger batch is fully time-ordered (stress of sort+publish).
TEST_F(FIFOSequencerTest, LargeBatchIsTimeOrdered) {
  for (int i = 99; i >= 0; --i) {
    sequencer->addClientRequest(1000 + i, makeReq(1, 100 + i));
  }
  sequencer->sequenceAndPublish();
  ASSERT_EQ(queue->size(), 100u);
  for (int i = 0; i < 100; ++i) {
    ASSERT_NE(queue->getNextToRead(), nullptr);
    EXPECT_EQ(queue->getNextToRead()->m_orderId, static_cast<OrderId>(100 + i));
    queue->updateReadIndex();
  }
}

// Verifies receive times are consumed (published requests carry no rx time):
// two requests with distinct times publish only their payloads in order.
TEST_F(FIFOSequencerTest, RxTimeIsNotPublishedOnlyPayloadOrder) {
  sequencer->addClientRequest(200, makeReq(2, 22));
  sequencer->addClientRequest(100, makeReq(1, 11));
  sequencer->sequenceAndPublish();
  ASSERT_EQ(queue->size(), 2u);
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 11u);
  queue->updateReadIndex();
  EXPECT_EQ(queue->getNextToRead()->m_orderId, 22u);
}
