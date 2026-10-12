#include <gtest/gtest.h>

#include "common/logging.hpp"
#include "trading/strategy/market_maker.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class MarketMakerTest : public ::testing::Test {
protected:
  static ClientRequestQueue *outReq;
  static ClientResponseQueue *inResp;
  static MarketUpdateQueue *inMd;
  static common::Logger *logger;

  TradeEngine *engine = nullptr;
  FeatureEngine *features = nullptr;
  PositionKeeper *keeper = nullptr;
  RiskManager *risk = nullptr;
  OrderManager *oms = nullptr;
  MarketMaker *maker = nullptr;
  MarketOrderBook *book = nullptr;
  TradeEngineConfigMap cfg{};

  static void SetUpTestSuite() {
    outReq = new ClientRequestQueue(4096);
    inResp = new ClientResponseQueue(4096);
    inMd = new MarketUpdateQueue(4096);
    logger = new common::Logger("test_market_maker.log");
  }

  static void TearDownTestSuite() {
    delete logger;
    delete outReq;
    delete inResp;
    delete inMd;
  }

  void SetUp() override {
    while (outReq->size()) outReq->updateReadIndex();
    cfg = TradeEngineConfigMap{};
    cfg[0].m_clip = 10;
    cfg[0].m_threshold = 5.0;
    cfg[0].m_riskConfig.m_maxOrderSize = 100;
    cfg[0].m_riskConfig.m_maxPosition = 1000;
    cfg[0].m_riskConfig.m_maxLoss = -1e9;

    engine = new TradeEngine(7, AlgoType::INVALID, cfg, outReq, inResp, inMd);
    features = new FeatureEngine(logger);
    keeper = new PositionKeeper(logger);
    risk = new RiskManager(keeper, cfg, logger);
    oms = new OrderManager(engine, *risk, logger);
    maker = new MarketMaker(engine, features, oms, cfg, logger);
    book = new MarketOrderBook(0, logger);
    book->setTradeEngine(engine);
  }

  void TearDown() override {
    // Destroy the engine before the book: the engine's position keeper holds
    // a pointer to this book's BBO and dereferences it while stopping.
    delete maker;
    delete oms;
    delete risk;
    delete keeper;
    delete features;
    delete engine;
    engine = nullptr;
    delete book;
    book = nullptr;
    while (outReq->size()) outReq->updateReadIndex();
    while (inResp->size()) inResp->updateReadIndex();
    while (inMd->size()) inMd->updateReadIndex();
  }

  void addToBook(OrderId id, common::Side s, Price p, Quantity q) {
    MEMarketUpdate u{MarketUpdateType::ADD, id, 0, s, p, q, 1};
    book->onMarketUpdate(&u);
  }

  // Builds a two-sided book with valid BBO (two updates per side, since the
  // book only refreshes BBO once the side already exists).
  void makeTwoSidedBook() {
    addToBook(1, common::Side::BUY, 100, 10);
    addToBook(2, common::Side::BUY, 100, 10);
    addToBook(3, common::Side::SELL, 102, 10);
    addToBook(4, common::Side::SELL, 102, 10);
  }

  void refreshFeatures() {
    features->onOrderBookUpdate(0, 101, common::Side::SELL, book);
  }

  // Drains requests emitted as a side effect of building the book: the book
  // is attached to the wired engine, so every ADD fires the maker's callback
  // through TradeEngine::onOrderBookUpdate. Tests below assert only on the
  // explicit maker->onOrderBookUpdate call under test.
  void drainOutReq() {
    while (outReq->size()) outReq->updateReadIndex();
  }
};

ClientRequestQueue *MarketMakerTest::outReq = nullptr;
ClientResponseQueue *MarketMakerTest::inResp = nullptr;
MarketUpdateQueue *MarketMakerTest::inMd = nullptr;
common::Logger *MarketMakerTest::logger = nullptr;

// Verifies no orders are sent when the book is one-sided.
TEST_F(MarketMakerTest, OneSidedBookSendsNothing) {
  addToBook(1, common::Side::BUY, 100, 10);
  refreshFeatures();
  maker->onOrderBookUpdate(0, 100, common::Side::BUY, book);
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies no orders are sent when only asks exist (bid BBO invalid).
TEST_F(MarketMakerTest, AskOnlyBookSendsNothing) {
  addToBook(1, common::Side::SELL, 102, 10);
  refreshFeatures();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies no orders are sent when the feature is still invalid (NaN),
// even with a two-sided book.
TEST_F(MarketMakerTest, InvalidFairPriceSendsNothing) {
  makeTwoSidedBook();
  // No refreshFeatures(): fair price stays NaN.
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies a fair price near mid posts both bid and ask quotes.
TEST_F(MarketMakerTest, FairMidPostsTwoSidedQuotes) {
  makeTwoSidedBook();
  refreshFeatures(); // fair = (100*20 + 102*20)/40 = 101.
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  EXPECT_EQ(outReq->size(), 2u);
}

// Verifies quotes use clip qty from config on both legs.
TEST_F(MarketMakerTest, QuotesUseClipFromConfig) {
  makeTwoSidedBook();
  refreshFeatures();
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  ASSERT_EQ(outReq->size(), 2u);
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    EXPECT_EQ(r->m_quantity, 10u);
    outReq->updateReadIndex();
  }
}

// Verifies a fair price within threshold of both BBO legs shades quotes by
// one tick to buy cheaper / sell richer: fair=101, bid=100 (diff 1 < 5) ->
// bid 99; ask=102 (diff 1 < 5) -> ask 103.
TEST_F(MarketMakerTest, NearMidShiftsQuotesByOne) {
  makeTwoSidedBook();
  refreshFeatures(); // fair 101.
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  ASSERT_EQ(outReq->size(), 2u);
  bool sawBid99 = false;
  bool sawAsk103 = false;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY && r->m_price == 99u) sawBid99 = true;
    if (r->m_side == common::Side::SELL && r->m_price == 103u) sawAsk103 = true;
    outReq->updateReadIndex();
  }
  EXPECT_TRUE(sawBid99);
  EXPECT_TRUE(sawAsk103);
}

// Verifies a fair price pinned at the bid holds the bid and lifts the ask:
// bid 100, ask 200, qty 10 each -> fair 150; threshold 5 -> bid holds 100,
// ask holds 200 (both diffs >= 5).
TEST_F(MarketMakerTest, FarFairHoldsQuotesAtBbo) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 200, 10);
  addToBook(4, common::Side::SELL, 200, 10);
  features->onOrderBookUpdate(0, 200, common::Side::SELL, book);
  drainOutReq();
  maker->onOrderBookUpdate(0, 200, common::Side::SELL, book);
  ASSERT_EQ(outReq->size(), 2u);
  bool sawBid100 = false;
  bool sawAsk200 = false;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY && r->m_price == 100u) sawBid100 = true;
    if (r->m_side == common::Side::SELL && r->m_price == 200u) sawAsk200 = true;
    outReq->updateReadIndex();
  }
  EXPECT_TRUE(sawBid100);
  EXPECT_TRUE(sawAsk200);
}

// Verifies trade updates are ignored by the market maker.
TEST_F(MarketMakerTest, TradeUpdateSendsNothing) {
  MEMarketUpdate t{MarketUpdateType::TRADE, common::INVALID_ORDER_ID, 0,
                   common::Side::BUY, 101, 5,
                   common::INVALID_PRIORITY};
  maker->onTradeUpdate(&t, book);
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies order updates are forwarded to the order manager (ACCEPTED -> LIVE).
TEST_F(MarketMakerTest, OrderUpdateForwardedToOrderManager) {
  makeTwoSidedBook();
  refreshFeatures();
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  ASSERT_EQ(outReq->size(), 2u);
  // Capture the actual BUY order id issued instead of assuming id 1.
  OrderId buyId = INVALID_ORDER_ID;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY) buyId = r->m_orderId;
    outReq->updateReadIndex();
  }
  ASSERT_NE(buyId, INVALID_ORDER_ID);

  MEClientResponse acc{ClientResponseType::ACCEPTED, 7, 0, buyId, 500,
                       common::Side::BUY, 100, 0, 10};
  maker->onOrderUpdate(&acc);
  EXPECT_EQ(oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
            OMOrderState::LIVE);
}
