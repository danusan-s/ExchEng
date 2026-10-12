#include <gtest/gtest.h>

#include "common/logging.hpp"
#include "exchange/matcher/matching_engine.hpp"

using namespace exchange;

// Shared engine; each test uses a distinct ticker to stay isolated.
class MatchingEngineTest : public ::testing::Test {
protected:
  static ClientRequestQueue *reqQ;
  static ClientResponseQueue *respQ;
  static MarketUpdateQueue *mdQ;
  static common::Logger *logger;
  static MatchingEngine *engine;

  static void SetUpTestSuite() {
    reqQ = new ClientRequestQueue(4096);
    respQ = new ClientResponseQueue(4096);
    mdQ = new MarketUpdateQueue(4096);
    logger = new common::Logger("test_matching_engine.log");
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
    while (respQ->size()) respQ->updateReadIndex();
    while (mdQ->size()) mdQ->updateReadIndex();
    while (reqQ->size()) reqQ->updateReadIndex();
  }

  MEClientRequest makeNew(TickerId t, ClientId c, OrderId o, common::Side s,
                          common::Price p, common::Quantity q) {
    return MEClientRequest{ClientRequestType::NEW, c, t, o, s, p, q};
  }
};

ClientRequestQueue *MatchingEngineTest::reqQ = nullptr;
ClientResponseQueue *MatchingEngineTest::respQ = nullptr;
MarketUpdateQueue *MatchingEngineTest::mdQ = nullptr;
common::Logger *MatchingEngineTest::logger = nullptr;
MatchingEngine *MatchingEngineTest::engine = nullptr;

// Verifies a NEW request routes to the book and yields ACCEPTED + ADD.
TEST_F(MatchingEngineTest, NewRequestProducesAcceptedAndAdd) {
  auto req = makeNew(0, 1, 11, common::Side::BUY, 100, 10);
  engine->processClientRequest(&req);

  ASSERT_GE(respQ->size(), 1u);
  EXPECT_EQ(respQ->getNextToRead()->m_type, ClientResponseType::ACCEPTED);
  ASSERT_GE(mdQ->size(), 1u);
  EXPECT_EQ(mdQ->getNextToRead()->m_type, MarketUpdateType::ADD);
}

// Verifies a CANCEL request removes a resting order with CANCELED.
TEST_F(MatchingEngineTest, CancelRequestRemovesRestingOrder) {
  auto req = makeNew(1, 1, 21, common::Side::BUY, 100, 10);
  engine->processClientRequest(&req);
  while (respQ->size()) respQ->updateReadIndex();
  while (mdQ->size()) mdQ->updateReadIndex();

  MEClientRequest cancel{ClientRequestType::CANCEL, 1, 1, 21,
                         common::Side::INVALID, 0, 0};
  engine->processClientRequest(&cancel);

  ASSERT_GE(respQ->size(), 1u);
  EXPECT_EQ(respQ->getNextToRead()->m_type, ClientResponseType::CANCELED);
}

// Verifies crossing NEW requests match through the engine queues.
TEST_F(MatchingEngineTest, CrossingOrdersMatchThroughEngine) {
  engine->processClientRequest(
      new MEClientRequest{ClientRequestType::NEW, 1, 2, 31,
                          common::Side::BUY, 100, 10});
  while (respQ->size()) respQ->updateReadIndex();
  while (mdQ->size()) mdQ->updateReadIndex();

  auto take = MEClientRequest{ClientRequestType::NEW, 2, 2, 32,
                              common::Side::SELL, 100, 10};
  engine->processClientRequest(&take);

  // ACCEPTED + 2x FILLED expected on the response queue.
  EXPECT_EQ(respQ->size(), 3u);
  // TRADE + CANCEL expected on the market queue.
  EXPECT_EQ(mdQ->size(), 2u);
}

// Verifies an INVALID request type is logged and ignored without queue output.
// NOTE: intentional divergence from the book, where processClientRequest
// FATALs (exits) on invalid types. This codebase logs and ignores so a bad
// client cannot crash the engine.
TEST_F(MatchingEngineTest, InvalidRequestTypeIsIgnored) {
  auto req = MEClientRequest{ClientRequestType::INVALID, 1, 3, 41,
                             common::Side::BUY, 100, 10};
  engine->processClientRequest(&req);
  EXPECT_EQ(respQ->size(), 0u);
  EXPECT_EQ(mdQ->size(), 0u);
}

// Verifies sendClientResponse enqueues exactly one response.
TEST_F(MatchingEngineTest, SendClientResponseEnqueuesOne) {
  MEClientResponse r{ClientResponseType::ACCEPTED, 5, 4, 51, 900,
                     common::Side::BUY, 100, 0, 10};
  engine->sendClientResponse(&r);
  ASSERT_EQ(respQ->size(), 1u);
  EXPECT_EQ(respQ->getNextToRead()->m_clientOrderId, 51u);
}

// Verifies sendMarketUpdate enqueues exactly one update.
TEST_F(MatchingEngineTest, SendMarketUpdateEnqueuesOne) {
  MEMarketUpdate u{MarketUpdateType::ADD, 900, 4, common::Side::BUY, 100, 10,
                   1};
  engine->sendMarketUpdate(&u);
  ASSERT_EQ(mdQ->size(), 1u);
  EXPECT_EQ(mdQ->getNextToRead()->m_qty, 10u);
}

// Verifies separate tickers maintain independent books, with a positive
// control: the same crossing pair on one ticker DOES match.
TEST_F(MatchingEngineTest, TickersAreIndependent) {
  auto bid = makeNew(5, 1, 61, common::Side::BUY, 100, 10);
  auto ask = makeNew(6, 2, 62, common::Side::SELL, 100, 10);
  engine->processClientRequest(&bid);
  engine->processClientRequest(&ask);
  // No cross-ticker match: both rest, so two ADDs and zero FILLs/TRADEs.
  size_t fills = 0;
  size_t adds = 0;
  for (auto *r = respQ->getNextToRead(); r;) {
    if (r->m_type == ClientResponseType::FILLED) ++fills;
    if (r->m_type == ClientResponseType::ACCEPTED) ++adds;
    respQ->updateReadIndex();
    r = respQ->getNextToRead();
  }
  EXPECT_EQ(fills, 0u);
  EXPECT_EQ(adds, 2u);
  while (mdQ->size()) mdQ->updateReadIndex();

  // Positive control: same prices on ticker 7 DO cross and fill.
  auto bid2 = makeNew(7, 1, 71, common::Side::BUY, 100, 10);
  engine->processClientRequest(&bid2);
  while (respQ->size()) respQ->updateReadIndex();
  while (mdQ->size()) mdQ->updateReadIndex();
  auto ask2 = makeNew(7, 2, 72, common::Side::SELL, 100, 10);
  engine->processClientRequest(&ask2);
  size_t fills2 = 0;
  for (auto *r = respQ->getNextToRead(); r;) {
    if (r->m_type == ClientResponseType::FILLED) ++fills2;
    respQ->updateReadIndex();
    r = respQ->getNextToRead();
  }
  EXPECT_EQ(fills2, 2u);
}
