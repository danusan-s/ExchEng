#include <gtest/gtest.h>

#include <vector>

#include "common/logging.hpp"
#include "exchange/matcher/matching_engine.hpp"
#include "exchange/matcher/me_order_book.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/order_server/client_request.hpp"
#include "exchange/order_server/client_response.hpp"

using namespace exchange;

// Shared engine/queues for all tests; each TEST gets a fresh isolated book.
class MEOrderBookTest : public ::testing::Test {
protected:
  static ClientRequestQueue *reqQ;
  static ClientResponseQueue *respQ;
  static MarketUpdateQueue *mdQ;
  static common::Logger *logger;
  static MatchingEngine *engine;

  MEOrderBook *book = nullptr;

  static void SetUpTestSuite() {
    reqQ = new ClientRequestQueue(4096);
    respQ = new ClientResponseQueue(4096);
    mdQ = new MarketUpdateQueue(4096);
    logger = new common::Logger("test_me_order_book.log");
    engine = new MatchingEngine(reqQ, respQ, mdQ);
  }

  static void TearDownTestSuite() {
    delete engine;
    engine = nullptr;
    delete logger;
    logger = nullptr;
    delete reqQ;
    delete mdQ;
    delete respQ;
  }

  void SetUp() override {
    drainQueues();
    book = new MEOrderBook(0, logger, engine);
  }

  void TearDown() override {
    delete book;
    book = nullptr;
    drainQueues();
  }

  void drainQueues() {
    while (respQ->size()) respQ->updateReadIndex();
    while (mdQ->size()) mdQ->updateReadIndex();
    while (reqQ->size()) reqQ->updateReadIndex();
  }

  std::vector<MEClientResponse> drainResponses() {
    std::vector<MEClientResponse> out;
    for (auto *r = respQ->getNextToRead(); r; r = respQ->getNextToRead()) {
      out.push_back(*r);
      respQ->updateReadIndex();
    }
    return out;
  }

  std::vector<MEMarketUpdate> drainUpdates() {
    std::vector<MEMarketUpdate> out;
    for (auto *u = mdQ->getNextToRead(); u; u = mdQ->getNextToRead()) {
      out.push_back(*u);
      mdQ->updateReadIndex();
    }
    return out;
  }
};

ClientRequestQueue *MEOrderBookTest::reqQ = nullptr;
ClientResponseQueue *MEOrderBookTest::respQ = nullptr;
MarketUpdateQueue *MEOrderBookTest::mdQ = nullptr;
common::Logger *MEOrderBookTest::logger = nullptr;
MatchingEngine *MEOrderBookTest::engine = nullptr;

// Verifies a lone bid rests on the book with ACCEPTED followed by ADD.
TEST_F(MEOrderBookTest, RestingBidProducesAcceptedAndAdd) {
  book->add(1, 100, 0, common::Side::BUY, 100, 10);

  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::ACCEPTED);
  EXPECT_EQ(resps[0].m_leavesQty, 10u);

  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::ADD);
  EXPECT_EQ(updates[0].m_side, common::Side::BUY);
  EXPECT_EQ(updates[0].m_price, 100u);
  EXPECT_EQ(updates[0].m_qty, 10u);
}

// Verifies a crossing sell fully lifts a resting bid with two FILLs and TRADE+CANCEL.
TEST_F(MEOrderBookTest, FullMatchGeneratesFillsTradeAndCancel) {
  book->add(1, 100, 0, common::Side::BUY, 100, 10);
  drainResponses();
  drainUpdates();

  book->add(2, 200, 0, common::Side::SELL, 100, 10);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  // ACCEPTED + FILLED(aggressor) + FILLED(passive).
  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::ACCEPTED);
  EXPECT_EQ(resps[1].m_type, ClientResponseType::FILLED);
  EXPECT_EQ(resps[1].m_execQty, 10u);
  EXPECT_EQ(resps[1].m_leavesQty, 0u);
  EXPECT_EQ(resps[2].m_type, ClientResponseType::FILLED);
  EXPECT_EQ(resps[2].m_clientId, 1u);

  // TRADE for the execution + CANCEL removing the fully filled resting order.
  ASSERT_EQ(updates.size(), 2u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[0].m_qty, 10u);
  EXPECT_EQ(updates[1].m_type, MarketUpdateType::CANCEL);
}

// Verifies a larger aggressor partially fills and leaves a MODIFY plus ADD remainder.
TEST_F(MEOrderBookTest, PartialFillLeavesRemainderWithModifyAndAdd) {
  book->add(1, 100, 0, common::Side::BUY, 100, 5);
  drainResponses();
  drainUpdates();

  book->add(2, 200, 0, common::Side::SELL, 100, 10);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[1].m_execQty, 5u);
  EXPECT_EQ(resps[1].m_leavesQty, 5u); // Aggressor still has 5 open.
  EXPECT_EQ(resps[2].m_leavesQty, 0u); // Passive fully consumed.

  ASSERT_EQ(updates.size(), 3u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[1].m_type, MarketUpdateType::CANCEL);
  EXPECT_EQ(updates[2].m_type, MarketUpdateType::ADD);
  EXPECT_EQ(updates[2].m_qty, 5u);
}

// Verifies FIFO priority at the same price fills the earlier order first.
TEST_F(MEOrderBookTest, SamePriceFifoPriorityFillsEarlierOrderFirst) {
  book->add(1, 101, 0, common::Side::BUY, 100, 5);
  book->add(2, 102, 0, common::Side::BUY, 100, 5);
  drainResponses();
  drainUpdates();

  book->add(3, 103, 0, common::Side::SELL, 100, 5);
  auto resps = drainResponses();

  ASSERT_EQ(resps.size(), 3u);
  // Passive fill must reference the first resting order (client 1).
  EXPECT_EQ(resps[2].m_clientId, 1u);
  EXPECT_EQ(resps[2].m_clientOrderId, 101u);
}

// Verifies price priority matches the best price first across levels.
TEST_F(MEOrderBookTest, BetterPriceMatchedFirst) {
  book->add(1, 101, 0, common::Side::SELL, 102, 5); // Worse ask.
  book->add(2, 102, 0, common::Side::SELL, 101, 5); // Better ask.
  drainResponses();
  drainUpdates();

  book->add(3, 103, 0, common::Side::BUY, 102, 5);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_GE(resps.size(), 3u);
  // Passive fill must come from the cheaper ask (client 2).
  EXPECT_EQ(resps[2].m_clientId, 2u);
  EXPECT_EQ(resps[2].m_price, 101u);
  ASSERT_GE(updates.size(), 1u);
  EXPECT_EQ(updates[0].m_price, 101u);
}

// Verifies cancelling a live order emits CANCELED plus a CANCEL market update.
TEST_F(MEOrderBookTest, CancelLiveOrderSucceeds) {
  book->add(1, 100, 0, common::Side::BUY, 100, 10);
  drainResponses();
  drainUpdates();

  book->cancel(1, 100, 0);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::CANCELED);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::CANCEL);
}

// Verifies cancelling an unknown order emits CANCEL_REJECTED with no market update.
TEST_F(MEOrderBookTest, CancelUnknownOrderIsRejected) {
  book->cancel(9, 999, 0);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::CANCEL_REJECTED);
  EXPECT_TRUE(updates.empty());
}

// Verifies a cancelled order cannot be cancelled twice.
TEST_F(MEOrderBookTest, DoubleCancelIsRejected) {
  book->add(1, 100, 0, common::Side::BUY, 100, 10);
  drainResponses();
  drainUpdates();

  book->cancel(1, 100, 0);
  drainResponses();
  drainUpdates();

  book->cancel(1, 100, 0);
  auto resps = drainResponses();
  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::CANCEL_REJECTED);
}

// Verifies a large buy sweeps two ask price levels in price order.
TEST_F(MEOrderBookTest, SweepAcrossTwoPriceLevels) {
  book->add(1, 101, 0, common::Side::SELL, 101, 5);
  book->add(2, 102, 0, common::Side::SELL, 102, 5);
  drainResponses();
  drainUpdates();

  book->add(3, 103, 0, common::Side::BUY, 102, 10);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  // ACCEPTED + 2x(FILLED aggressor + FILLED passive) = 5 responses.
  EXPECT_EQ(resps.size(), 5u);
  // 2x TRADE + 2x CANCEL, no ADD since fully filled.
  ASSERT_EQ(updates.size(), 4u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[0].m_price, 101u); // Cheaper level first.
  EXPECT_EQ(updates[2].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[2].m_price, 102u);
}

// Verifies a non-crossing sell rests when no bids exist.
TEST_F(MEOrderBookTest, NonCrossingSellRestsOnBook) {
  book->add(1, 100, 0, common::Side::SELL, 110, 7);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 1u);
  EXPECT_EQ(resps[0].m_type, ClientResponseType::ACCEPTED);
  ASSERT_EQ(updates.size(), 1u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::ADD);
  EXPECT_EQ(updates[0].m_side, common::Side::SELL);
}

// Verifies a smaller aggressor partially fills a larger passive order:
// passive survives with MODIFY (not CANCEL) and no ADD for the aggressor.
TEST_F(MEOrderBookTest, PassivePartialFillProducesModify) {
  book->add(1, 100, 0, common::Side::SELL, 100, 10);
  drainResponses();
  drainUpdates();

  book->add(2, 200, 0, common::Side::BUY, 100, 4);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  // ACCEPTED + FILLED(aggressor, leaves 0) + FILLED(passive, leaves 6).
  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[1].m_execQty, 4u);
  EXPECT_EQ(resps[1].m_leavesQty, 0u);
  EXPECT_EQ(resps[2].m_execQty, 4u);
  EXPECT_EQ(resps[2].m_leavesQty, 6u);

  // TRADE + MODIFY (passive remainder 6), no CANCEL, no ADD.
  ASSERT_EQ(updates.size(), 2u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[0].m_qty, 4u);
  EXPECT_EQ(updates[1].m_type, MarketUpdateType::MODIFY);
  EXPECT_EQ(updates[1].m_qty, 6u);
}

// Verifies fills execute at the passive (resting) price, not the aggressor's
// limit price: sell at 90 into bid at 100 fills at 100.
TEST_F(MEOrderBookTest, FillPriceIsPassivePrice) {
  book->add(1, 100, 0, common::Side::BUY, 100, 10);
  drainResponses();
  drainUpdates();

  book->add(2, 200, 0, common::Side::SELL, 90, 10);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  ASSERT_EQ(resps.size(), 3u);
  EXPECT_EQ(resps[1].m_price, 100u); // aggressor fill at passive price.
  EXPECT_EQ(resps[2].m_price, 100u); // passive fill at its own price.
  ASSERT_GE(updates.size(), 1u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[0].m_price, 100u);
  EXPECT_EQ(updates[0].m_qty, 10u);
}

// Verifies a sweep splits the aggressor fill across levels with per-level
// execQty and leavesQty tracking.
TEST_F(MEOrderBookTest, SweepSplitExecQtyPerLevel) {
  book->add(1, 101, 0, common::Side::SELL, 101, 5);
  book->add(2, 102, 0, common::Side::SELL, 102, 7);
  drainResponses();
  drainUpdates();

  book->add(3, 103, 0, common::Side::BUY, 102, 10);
  auto resps = drainResponses();
  auto updates = drainUpdates();

  // ACCEPTED + (FILLED agg 5, leaves 5 + FILLED passive) + (FILLED agg 5,
  // leaves 0 + FILLED passive).
  ASSERT_EQ(resps.size(), 5u);
  EXPECT_EQ(resps[1].m_execQty, 5u);
  EXPECT_EQ(resps[1].m_leavesQty, 5u);
  EXPECT_EQ(resps[3].m_execQty, 5u);
  EXPECT_EQ(resps[3].m_leavesQty, 0u);
  // Second passive leg leaves 2 resting.
  EXPECT_EQ(resps[4].m_leavesQty, 2u);
  // TRADE(5@101) + CANCEL + TRADE(5@102) + MODIFY(2 resting).
  ASSERT_EQ(updates.size(), 4u);
  EXPECT_EQ(updates[0].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[0].m_price, 101u);
  EXPECT_EQ(updates[0].m_qty, 5u);
  EXPECT_EQ(updates[1].m_type, MarketUpdateType::CANCEL);
  EXPECT_EQ(updates[2].m_type, MarketUpdateType::TRADE);
  EXPECT_EQ(updates[2].m_price, 102u);
  EXPECT_EQ(updates[3].m_type, MarketUpdateType::MODIFY);
  EXPECT_EQ(updates[3].m_qty, 2u);
}
