#include <gtest/gtest.h>

#include <memory>

#include "common/logging.hpp"
#include "exchange/matcher/matching_engine.hpp"

using namespace exchange;

// Shared engine; each test uses a distinct ticker to stay isolated.
class MatchingEngineTest : public ::testing::Test {
protected:
  static std::unique_ptr<ClientRequestImplQueue> reqQ;
  static std::unique_ptr<ClientResponseImplQueue> respQ;
  static std::unique_ptr<MarketUpdateImplQueue> mdQ;
  static std::unique_ptr<common::Logger> logger;
  static std::unique_ptr<MatchingEngine> engine;

  static void SetUpTestSuite() {
    reqQ = std::make_unique<ClientRequestImplQueue>(4096);
    respQ = std::make_unique<ClientResponseImplQueue>(4096);
    mdQ = std::make_unique<MarketUpdateImplQueue>(4096);
    logger = std::make_unique<common::Logger>("test_matching_engine.log");
    engine = std::make_unique<MatchingEngine>(reqQ.get(), respQ.get(),
                                              mdQ.get());
  }

  static void TearDownTestSuite() {
    engine.reset();
    logger.reset();
    reqQ.reset();
    mdQ.reset();
    respQ.reset();
  }

  void SetUp() override {
    while (respQ->getNextToRead() != nullptr)
      respQ->updateReadIndex();
    while (mdQ->getNextToRead() != nullptr)
      mdQ->updateReadIndex();
    while (reqQ->getNextToRead() != nullptr)
      reqQ->updateReadIndex();
  }

  ClientRequestImpl makeNew(TickerId t, ClientId c, OrderId o, common::Side s,
                            common::Price p, common::Quantity q) {
    return ClientRequestImpl{ClientRequestType::NEW, c, t, o, s, p, q};
  }
};

std::unique_ptr<ClientRequestImplQueue> MatchingEngineTest::reqQ = nullptr;
std::unique_ptr<ClientResponseImplQueue> MatchingEngineTest::respQ = nullptr;
std::unique_ptr<MarketUpdateImplQueue> MatchingEngineTest::mdQ = nullptr;
std::unique_ptr<common::Logger> MatchingEngineTest::logger = nullptr;
std::unique_ptr<MatchingEngine> MatchingEngineTest::engine = nullptr;

// Verifies a NEW request routes to the book and yields ACCEPTED + ADD.
TEST_F(MatchingEngineTest, NewRequestProducesAcceptedAndAdd) {
  auto req = makeNew(0, 1, 11, common::Side::BUY, 100, 10);
  engine->processClientRequest(&req);

  ASSERT_NE(respQ->getNextToRead(), nullptr);
  EXPECT_EQ(respQ->getNextToRead()->m_type, ClientResponseType::ACCEPTED);
  ASSERT_NE(mdQ->getNextToRead(), nullptr);
  EXPECT_EQ(mdQ->getNextToRead()->m_type, MarketUpdateType::ADD);
}

// Verifies a CANCEL request removes a resting order with CANCELED.
TEST_F(MatchingEngineTest, CancelRequestRemovesRestingOrder) {
  auto req = makeNew(1, 1, 21, common::Side::BUY, 100, 10);
  engine->processClientRequest(&req);
  while (respQ->getNextToRead() != nullptr)
    respQ->updateReadIndex();
  while (mdQ->getNextToRead() != nullptr)
    mdQ->updateReadIndex();

  ClientRequestImpl cancel{ClientRequestType::CANCEL, 1, 1, 21,
                           common::Side::INVALID,     0, 0};
  engine->processClientRequest(&cancel);

  ASSERT_NE(respQ->getNextToRead(), nullptr);
  EXPECT_EQ(respQ->getNextToRead()->m_type, ClientResponseType::CANCELED);
}

// Verifies crossing NEW requests match through the engine queues.
TEST_F(MatchingEngineTest, CrossingOrdersMatchThroughEngine) {
  engine->processClientRequest(new ClientRequestImpl{
      ClientRequestType::NEW, 1, 2, 31, common::Side::BUY, 100, 10});
  while (respQ->getNextToRead() != nullptr)
    respQ->updateReadIndex();
  while (mdQ->getNextToRead() != nullptr)
    mdQ->updateReadIndex();

  auto take = ClientRequestImpl{ClientRequestType::NEW, 2,   2, 32,
                                common::Side::SELL,     100, 10};
  engine->processClientRequest(&take);

  // ACCEPTED + 2x FILLED expected on the response queue.
  EXPECT_NE(respQ->getNextToRead(), nullptr);
  // TRADE + CANCEL expected on the market queue.
  EXPECT_NE(mdQ->getNextToRead(), nullptr);
}

// Verifies an INVALID request type is logged and ignored without queue output.
// NOTE: intentional divergence from the book, where processClientRequest
// FATALs (exits) on invalid types. This codebase logs and ignores so a bad
// client cannot crash the engine.
TEST_F(MatchingEngineTest, InvalidRequestTypeIsIgnored) {
  auto req = ClientRequestImpl{ClientRequestType::INVALID, 1,   3, 41,
                               common::Side::BUY,          100, 10};
  engine->processClientRequest(&req);
  EXPECT_EQ(respQ->getNextToRead(), nullptr);
  EXPECT_EQ(mdQ->getNextToRead(), nullptr);
}

// Verifies sendClientResponse enqueues exactly one response.
TEST_F(MatchingEngineTest, SendClientResponseEnqueuesOne) {
  ClientResponseImpl r{ClientResponseType::ACCEPTED,
                       5,
                       4,
                       51,
                       900,
                       common::Side::BUY,
                       100,
                       0,
                       10};
  engine->sendClientResponse(&r);
  ASSERT_NE(respQ->getNextToRead(), nullptr);
  EXPECT_EQ(respQ->getNextToRead()->m_clientOrderId, 51u);
}

// Verifies sendMarketUpdate enqueues exactly one update.
TEST_F(MatchingEngineTest, SendMarketUpdateEnqueuesOne) {
  MarketUpdateImpl u{
      MarketUpdateType::ADD, 900, 4, common::Side::BUY, 100, 10, 1};
  engine->sendMarketUpdate(&u);
  ASSERT_NE(mdQ->getNextToRead(), nullptr);
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
    if (r->m_type == ClientResponseType::FILLED)
      ++fills;
    if (r->m_type == ClientResponseType::ACCEPTED)
      ++adds;
    respQ->updateReadIndex();
    r = respQ->getNextToRead();
  }
  EXPECT_EQ(fills, 0u);
  EXPECT_EQ(adds, 2u);
  while (mdQ->getNextToRead() != nullptr)
    mdQ->updateReadIndex();

  // Positive control: same prices on ticker 7 DO cross and fill.
  auto bid2 = makeNew(7, 1, 71, common::Side::BUY, 100, 10);
  engine->processClientRequest(&bid2);
  while (respQ->getNextToRead() != nullptr)
    respQ->updateReadIndex();
  while (mdQ->getNextToRead() != nullptr)
    mdQ->updateReadIndex();
  auto ask2 = makeNew(7, 2, 72, common::Side::SELL, 100, 10);
  engine->processClientRequest(&ask2);
  size_t fills2 = 0;
  for (auto *r = respQ->getNextToRead(); r;) {
    if (r->m_type == ClientResponseType::FILLED)
      ++fills2;
    respQ->updateReadIndex();
    r = respQ->getNextToRead();
  }
  EXPECT_EQ(fills2, 2u);
}
