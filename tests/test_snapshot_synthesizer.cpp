#include <gtest/gtest.h>

#include <memory>

#include "common/logging.hpp"
#include "exchange/market_data/snapshot_synthesizer.hpp"

using namespace exchange;

// Each test uses a fresh synthesizer (own seq counter + order map).
class SnapshotTest : public ::testing::Test {
protected:
  std::unique_ptr<MarketUpdateQueue> queue = nullptr;
  std::unique_ptr<common::Logger> logger = nullptr;
  std::unique_ptr<SnapshotSynthesizer> synth = nullptr;
  static int portCounter;

  void SetUp() override {
    queue = std::make_unique<MarketUpdateQueue>(1024);
    logger = std::make_unique<common::Logger>("test_snapshot.log");
    // Fresh socket per test on loopback with a distinct port.
    synth = std::make_unique<SnapshotSynthesizer>(queue.get(), "lo",
                                                  "239.0.0.1",
                                                  19000 + (portCounter++));
  }

  void TearDown() override {
    synth.reset();
    logger.reset();
    queue.reset();
  }

  MarketUpdate makeUpdate(size_t seq, MarketUpdateType type, OrderId oid,
                          TickerId t = 0, common::Side s = common::Side::BUY,
                          common::Price p = 100, common::Quantity q = 10,
                          common::Priority pr = 1) {
    MarketUpdate u;
    u.m_seqNum = seq;
    u.m_marketUpdate = MarketUpdateImpl(type, oid, t, s, p, q, pr);
    return u;
  }
};

int SnapshotTest::portCounter = 0;

// Verifies an ADD update is tracked and advances the expected sequence.
TEST_F(SnapshotTest, AddIsTrackedAndAdvancesSequence) {
  auto u = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&u);
  EXPECT_EQ(synth->getLastSeqNum(), 1u);
  const auto *tracked = synth->getSnapshotOrder(0, 5);
  ASSERT_NE(tracked, nullptr);
  EXPECT_EQ(tracked->m_price, 100u);
  EXPECT_EQ(tracked->m_qty, 10u);
}

// Verifies MODIFY changes price/qty of a previously added order.
TEST_F(SnapshotTest, ModifyUpdatesTrackedOrder) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&add);
  auto mod = makeUpdate(2, MarketUpdateType::MODIFY, 5, 0, common::Side::BUY,
                        101, 7, 1);
  synth->addToSnapshot(&mod);
  EXPECT_EQ(synth->getLastSeqNum(), 2u);
  const auto *tracked = synth->getSnapshotOrder(0, 5);
  ASSERT_NE(tracked, nullptr);
  EXPECT_EQ(tracked->m_price, 101u);
  EXPECT_EQ(tracked->m_qty, 7u);
}

// Verifies CANCEL removes a previously added order from the snapshot.
TEST_F(SnapshotTest, CancelRemovesTrackedOrder) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&add);
  ASSERT_NE(synth->getSnapshotOrder(0, 5), nullptr);
  auto cancel = makeUpdate(2, MarketUpdateType::CANCEL, 5);
  synth->addToSnapshot(&cancel);
  EXPECT_EQ(synth->getSnapshotOrder(0, 5), nullptr);
  EXPECT_EQ(synth->getLastSeqNum(), 2u);
}

// Verifies TRADE updates are ignored by the snapshot image but still advance
// the sequence (book checks seq for every message type).
TEST_F(SnapshotTest, TradeIsIgnoredButAdvancesSeq) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&add);
  auto trade = makeUpdate(2, MarketUpdateType::TRADE, 0);
  synth->addToSnapshot(&trade);
  EXPECT_EQ(synth->getLastSeqNum(), 2u);
  EXPECT_NE(synth->getSnapshotOrder(0, 5), nullptr);
}

// Verifies CLEAR updates are ignored by the snapshot image but advance seq.
TEST_F(SnapshotTest, ClearIsIgnoredButAdvancesSeq) {
  auto u = makeUpdate(1, MarketUpdateType::CLEAR, 0);
  synth->addToSnapshot(&u);
  EXPECT_EQ(synth->getLastSeqNum(), 1u);
}

// Verifies SNAPSHOT_START/END markers are ignored by the snapshot image.
TEST_F(SnapshotTest, SnapshotMarkersAreIgnored) {
  auto s = makeUpdate(1, MarketUpdateType::SNAPSHOT_START, 0);
  auto e = makeUpdate(2, MarketUpdateType::SNAPSHOT_END, 0);
  synth->addToSnapshot(&s);
  synth->addToSnapshot(&e);
  EXPECT_EQ(synth->getLastSeqNum(), 2u);
}

// Verifies orders on different tickers are tracked independently.
TEST_F(SnapshotTest, TickersTrackedIndependently) {
  auto a0 = makeUpdate(1, MarketUpdateType::ADD, 5, 0);
  auto a1 = makeUpdate(2, MarketUpdateType::ADD, 5, 1);
  synth->addToSnapshot(&a0);
  synth->addToSnapshot(&a1);
  EXPECT_NE(synth->getSnapshotOrder(0, 5), nullptr);
  EXPECT_NE(synth->getSnapshotOrder(1, 5), nullptr);
  // Cancel on ticker 0 must not affect ticker 1.
  auto c0 = makeUpdate(3, MarketUpdateType::CANCEL, 5, 0);
  synth->addToSnapshot(&c0);
  EXPECT_EQ(synth->getSnapshotOrder(0, 5), nullptr);
  EXPECT_NE(synth->getSnapshotOrder(1, 5), nullptr);
}

// Verifies an ADD-CANCEL-ADD cycle on the same order id works.
TEST_F(SnapshotTest, ReAddAfterCancelWorks) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5);
  auto cancel = makeUpdate(2, MarketUpdateType::CANCEL, 5);
  auto readd =
      makeUpdate(3, MarketUpdateType::ADD, 5, 0, common::Side::BUY, 105, 3, 1);
  synth->addToSnapshot(&add);
  synth->addToSnapshot(&cancel);
  synth->addToSnapshot(&readd);
  const auto *tracked = synth->getSnapshotOrder(0, 5);
  ASSERT_NE(tracked, nullptr);
  EXPECT_EQ(tracked->m_price, 105u);
  EXPECT_EQ(tracked->m_qty, 3u);
}

// Verifies a duplicate ADD trips the exists ASSERT (death).
TEST_F(SnapshotTest, DuplicateAddDies) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&add);
  auto dup = makeUpdate(2, MarketUpdateType::ADD, 5);
  EXPECT_DEATH({ synth->addToSnapshot(&dup); }, ".*");
}

// Verifies MODIFY of a missing order trips the exists ASSERT (death).
TEST_F(SnapshotTest, ModifyMissingDies) {
  auto mod = makeUpdate(1, MarketUpdateType::MODIFY, 9);
  EXPECT_DEATH({ synth->addToSnapshot(&mod); }, ".*");
}

// Verifies CANCEL of a missing order trips the exists ASSERT (death).
TEST_F(SnapshotTest, CancelMissingDies) {
  auto cancel = makeUpdate(1, MarketUpdateType::CANCEL, 9);
  EXPECT_DEATH({ synth->addToSnapshot(&cancel); }, ".*");
}

// Verifies a sequence gap trips the contiguity ASSERT (death).
TEST_F(SnapshotTest, SeqGapDies) {
  auto a1 = makeUpdate(1, MarketUpdateType::ADD, 5);
  synth->addToSnapshot(&a1);
  auto gap = makeUpdate(3, MarketUpdateType::ADD, 6);
  EXPECT_DEATH({ synth->addToSnapshot(&gap); }, ".*");
}

// Verifies MODIFY with a mismatched side trips the side ASSERT (death).
TEST_F(SnapshotTest, ModifyWrongSideDies) {
  auto add = makeUpdate(1, MarketUpdateType::ADD, 5, 0, common::Side::BUY);
  synth->addToSnapshot(&add);
  auto mod = makeUpdate(2, MarketUpdateType::MODIFY, 5, 0, common::Side::SELL,
                        100, 10, 1);
  EXPECT_DEATH({ synth->addToSnapshot(&mod); }, ".*");
}
