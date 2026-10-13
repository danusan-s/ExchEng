#include <chrono>
#include <gtest/gtest.h>

#include <thread>

#include "common/logging.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class TradeEngineTest : public ::testing::Test {
protected:
  ClientRequestImplQueue *outReq = nullptr;
  ClientResponseImplQueue *inResp = nullptr;
  MarketUpdateImplQueue *inMd = nullptr;
  TradeEngine *engine = nullptr;
  TradeEngineConfigMap cfg{};

  void SetUp() override {
    outReq = new ClientRequestImplQueue(1024);
    inResp = new ClientResponseImplQueue(1024);
    inMd = new MarketUpdateImplQueue(1024);
    cfg = TradeEngineConfigMap{};
    engine = new TradeEngine(7, AlgoType::INVALID, cfg, outReq, inResp, inMd);
  }

  void TearDown() override {
    while (inResp->size())
      inResp->updateReadIndex();
    while (inMd->size())
      inMd->updateReadIndex();
    while (outReq->size())
      outReq->updateReadIndex();
    delete engine;
    engine = nullptr;
    delete outReq;
    delete inResp;
    delete inMd;
  }
};

// Verifies the engine reports its configured client id.
TEST_F(TradeEngineTest, ReportsClientId) {
  EXPECT_EQ(engine->getClientId(), 7u);
}

// Verifies sendClientRequest enqueues the request for the gateway.
TEST_F(TradeEngineTest, SendEnqueuesRequest) {
  ClientRequestImpl req{ClientRequestType::NEW, 7,   0, 1,
                        common::Side::BUY,      100, 10};
  engine->sendClientRequest(&req);
  ASSERT_EQ(outReq->size(), 1u);
  EXPECT_EQ(outReq->getNextToRead()->m_orderId, 1u);
}

// Verifies onOrderUpdate with FILL updates the position keeper.
TEST_F(TradeEngineTest, FillUpdatesPosition) {
  ClientResponseImpl f{ClientResponseType::FILLED, 7,   0,  1, 500,
                       common::Side::BUY,          100, 10, 0};
  engine->onOrderUpdate(&f);
  EXPECT_EQ(engine->getPositionInfo(0).m_position, 10);
  EXPECT_EQ(engine->getPositionInfo(0).m_volume, 10u);
  // No crash and no outgoing request from the default (logging) algo.
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies onOrderUpdate with ACCEPTED leaves positions flat.
TEST_F(TradeEngineTest, AcceptLeavesPositionFlat) {
  ClientResponseImpl a{ClientResponseType::ACCEPTED,
                       7,
                       0,
                       1,
                       500,
                       common::Side::BUY,
                       100,
                       0,
                       10};
  engine->onOrderUpdate(&a);
  EXPECT_EQ(engine->getPositionInfo(0).m_position, 0);
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies onOrderBookUpdate refreshes unreal PnL via the position keeper:
// open a long, then feed two-sided ADDs through the engine's own book 0
// (MarketOrderBook::onMarketUpdate calls back into the engine). The book
// needs two updates per side before its BBO refreshes.
TEST_F(TradeEngineTest, OrderBookUpdateRefreshesUnreal) {
  ClientResponseImpl f{ClientResponseType::FILLED, 7,   0,  1, 500,
                       common::Side::BUY,          100, 10, 0};
  engine->onOrderUpdate(&f);
  ASSERT_DOUBLE_EQ(engine->getPositionInfo(0).m_unrealPnL, 0.0);

  auto *book = engine->getOrderBook(0);
  const MarketUpdateImpl b1{
      MarketUpdateType::ADD, 1, 0, common::Side::BUY, 110, 10, 1};
  const MarketUpdateImpl b2{
      MarketUpdateType::ADD, 2, 0, common::Side::BUY, 110, 10, 1};
  const MarketUpdateImpl a1{
      MarketUpdateType::ADD, 3, 0, common::Side::SELL, 112, 10, 1};
  const MarketUpdateImpl a2{
      MarketUpdateType::ADD, 4, 0, common::Side::SELL, 112, 10, 1};
  book->onMarketUpdate(&b1);
  book->onMarketUpdate(&b2);
  book->onMarketUpdate(&a1);
  book->onMarketUpdate(&a2);
  // Mid 111 vs VWAP 100 over 10 lots -> +110 unreal.
  EXPECT_DOUBLE_EQ(engine->getPositionInfo(0).m_unrealPnL, 110.0);
}

// Verifies the default algo callbacks tolerate updates without crashing.
TEST_F(TradeEngineTest, DefaultAlgoCallbacksAreSafe) {
  engine->defaultAlgoOnOrderBookUpdate(0, 100, common::Side::BUY, nullptr);
  MarketUpdateImpl u{
      MarketUpdateType::ADD, 1, 0, common::Side::BUY, 100, 10, 1};
  engine->defaultAlgoOnTradeUpdate(&u, nullptr);
  ClientResponseImpl a{ClientResponseType::ACCEPTED,
                       7,
                       0,
                       1,
                       500,
                       common::Side::BUY,
                       100,
                       0,
                       10};
  engine->defaultAlgoOnOrderUpdate(&a);
  SUCCEED();
}

// Verifies a MAKER engine wires algo callbacks (book update posts quotes).
TEST_F(TradeEngineTest, MakerEnginePostsQuotesOnBookUpdate) {
  TradeEngineConfigMap makerCfg{};
  makerCfg[0].m_clip = 10;
  makerCfg[0].m_threshold = 500.0;
  makerCfg[0].m_riskConfig.m_maxOrderSize = 100;
  makerCfg[0].m_riskConfig.m_maxPosition = 1000;
  makerCfg[0].m_riskConfig.m_maxLoss = -1e9;
  ClientRequestImplQueue rq(1024);
  ClientResponseImplQueue rs(1024);
  MarketUpdateImplQueue md(1024);
  TradeEngine maker(9, AlgoType::MAKER, makerCfg, &rq, &rs, &md);
  // Drive two-sided ADDs through the maker's own book 0 via md queue + run
  // equivalent: use onOrderBookUpdate path with a scratch book is not the
  // wiring; instead verify the algo callback was overridden (not default) by
  // checking a book update yields requests. Feed market data through run()
  // briefly.
  MarketUpdateImpl b1{
      MarketUpdateType::ADD, 1, 0, common::Side::BUY, 100, 10, 1};
  MarketUpdateImpl b2{
      MarketUpdateType::ADD, 2, 0, common::Side::BUY, 100, 10, 1};
  MarketUpdateImpl a1{
      MarketUpdateType::ADD, 3, 0, common::Side::SELL, 102, 10, 1};
  MarketUpdateImpl a2{
      MarketUpdateType::ADD, 4, 0, common::Side::SELL, 102, 10, 1};
  for (const auto *u : {&b1, &b2, &a1, &a2}) {
    auto *w = md.getNextToWriteTo();
    *w = *u;
    md.updateWriteIndex();
  }
  maker.start();
  // Poll until the maker posts quotes (or timeout): run() consumes md and
  // the maker algo's onOrderBookUpdate issues moveOrders -> requests.
  auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (rq.size() == 0u && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  maker.stop();
  EXPECT_GT(rq.size(), 0u);
}

// Verifies stringToAlgoType round-trips known algo names.
TEST_F(TradeEngineTest, AlgoTypeStringRoundTrip) {
  EXPECT_EQ(stringToAlgoType("MAKER"), AlgoType::MAKER);
  EXPECT_EQ(stringToAlgoType("TAKER"), AlgoType::TAKER);
  EXPECT_EQ(stringToAlgoType("RANDOM"), AlgoType::RANDOM);
  EXPECT_EQ(stringToAlgoType("NOPE"), AlgoType::INVALID);
}
