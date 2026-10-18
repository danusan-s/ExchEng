#include <gtest/gtest.h>
#include <memory>

#include "common/logging.hpp"
#include "trading/trader/market_maker.hpp"
#include "trading/trader/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class MarketMakerTest : public ::testing::Test {
protected:
  static std::unique_ptr<ClientRequestImplQueue> outReq;
  static std::unique_ptr<ClientResponseImplQueue> inResp;
  static std::unique_ptr<MarketUpdateImplQueue> inMd;
  static std::unique_ptr<common::Logger> logger;

  std::unique_ptr<TradeEngine> engine = nullptr;
  std::unique_ptr<FeatureEngine> features = nullptr;
  std::unique_ptr<PositionKeeper> keeper = nullptr;
  std::unique_ptr<RiskManager> risk = nullptr;
  std::unique_ptr<OrderManager> oms = nullptr;
  std::unique_ptr<MarketMaker> maker = nullptr;
  std::unique_ptr<MarketOrderBook> book = nullptr;
  TradeEngineConfigMap cfg{};

  static void SetUpTestSuite() {
    outReq = std::make_unique<ClientRequestImplQueue>(4096);
    inResp = std::make_unique<ClientResponseImplQueue>(4096);
    inMd = std::make_unique<MarketUpdateImplQueue>(4096);
    logger = std::make_unique<common::Logger>("test_market_maker.log");
  }

  static void TearDownTestSuite() {
    logger.reset();
    outReq.reset();
    inResp.reset();
    inMd.reset();
  }

  void SetUp() override {
    while (outReq->getNextToRead() != nullptr)
      outReq->updateReadIndex();
    cfg = TradeEngineConfigMap{};
    cfg.at(0).m_clip = 10;
    cfg.at(0).m_threshold = 5.0;
    cfg.at(0).m_riskConfig.m_maxOrderSize = 100;
    cfg.at(0).m_riskConfig.m_maxPosition = 1000;
    cfg.at(0).m_riskConfig.m_maxLoss = -1e9;

    engine = std::make_unique<TradeEngine>(7, AlgoType::INVALID, cfg,
                                           outReq.get(), inResp.get(),
                                           inMd.get());
    features = std::make_unique<FeatureEngine>(logger.get());
    keeper = std::make_unique<PositionKeeper>(logger.get());
    risk = std::make_unique<RiskManager>(keeper.get(), cfg, logger.get());
    oms = std::make_unique<OrderManager>(engine.get(), *risk, logger.get());
    maker = std::make_unique<MarketMaker>(engine.get(), features.get(),
                                          oms.get(), cfg, logger.get());
    book = std::make_unique<MarketOrderBook>(0, logger.get());
    book->setTradeEngine(engine.get());
  }

  void TearDown() override {
    // Destroy the engine before the book: the engine's position keeper holds
    // a pointer to this book's BBO and dereferences it while stopping.
    maker.reset();
    oms.reset();
    risk.reset();
    keeper.reset();
    features.reset();
    engine.reset();
    book.reset();
    while (outReq->getNextToRead() != nullptr)
      outReq->updateReadIndex();
    while (inResp->getNextToRead() != nullptr)
      inResp->updateReadIndex();
    while (inMd->getNextToRead() != nullptr)
      inMd->updateReadIndex();
  }

  void addToBook(OrderId id, common::Side s, Price p, Quantity q) {
    MarketUpdateImpl u(MarketUpdateType::ADD, id, 0, s, p, q, 1);
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
    features->onOrderBookUpdate(0, 101, common::Side::SELL, book.get());
  }

  // Drains requests emitted as a side effect of building the book: the book
  // is attached to the wired engine, so every ADD fires the maker's callback
  // through TradeEngine::onOrderBookUpdate. Tests below assert only on the
  // explicit maker->onOrderBookUpdate call under test.
  void drainOutReq() {
    while (outReq->getNextToRead() != nullptr)
      outReq->updateReadIndex();
  }
};

std::unique_ptr<ClientRequestImplQueue> MarketMakerTest::outReq = nullptr;
std::unique_ptr<ClientResponseImplQueue> MarketMakerTest::inResp = nullptr;
std::unique_ptr<MarketUpdateImplQueue> MarketMakerTest::inMd = nullptr;
std::unique_ptr<common::Logger> MarketMakerTest::logger = nullptr;

// Verifies no orders are sent when the book is one-sided.
TEST_F(MarketMakerTest, OneSidedBookSendsNothing) {
  addToBook(1, common::Side::BUY, 100, 10);
  refreshFeatures();
  maker->onOrderBookUpdate(0, 100, common::Side::BUY, book.get());
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies no orders are sent when only asks exist (bid BBO invalid).
TEST_F(MarketMakerTest, AskOnlyBookSendsNothing) {
  addToBook(1, common::Side::SELL, 102, 10);
  refreshFeatures();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies no orders are sent when the feature is still invalid (NaN),
// even with a two-sided book.
TEST_F(MarketMakerTest, InvalidFairPriceSendsNothing) {
  makeTwoSidedBook();
  // No refreshFeatures(): fair price stays NaN.
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies a fair price near mid posts both bid and ask quotes.
TEST_F(MarketMakerTest, FairMidPostsTwoSidedQuotes) {
  makeTwoSidedBook();
  refreshFeatures(); // fair = (100*20 + 102*20)/40 = 101.
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  EXPECT_NE(outReq->getNextToRead(), nullptr);
}

// Verifies quotes use clip qty from config on both legs.
TEST_F(MarketMakerTest, QuotesUseClipFromConfig) {
  makeTwoSidedBook();
  refreshFeatures();
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  ASSERT_NE(outReq->getNextToRead(), nullptr);
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
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  bool sawBid99 = false;
  bool sawAsk103 = false;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY && r->m_price == 99u)
      sawBid99 = true;
    if (r->m_side == common::Side::SELL && r->m_price == 103u)
      sawAsk103 = true;
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
  features->onOrderBookUpdate(0, 200, common::Side::SELL, book.get());
  drainOutReq();
  maker->onOrderBookUpdate(0, 200, common::Side::SELL, book.get());
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  bool sawBid100 = false;
  bool sawAsk200 = false;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY && r->m_price == 100u)
      sawBid100 = true;
    if (r->m_side == common::Side::SELL && r->m_price == 200u)
      sawAsk200 = true;
    outReq->updateReadIndex();
  }
  EXPECT_TRUE(sawBid100);
  EXPECT_TRUE(sawAsk200);
}

// Verifies trade updates are ignored by the market maker.
TEST_F(MarketMakerTest, TradeUpdateSendsNothing) {
  MarketUpdateImpl t(MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     101,
                     5,
                     common::INVALID_PRIORITY);
  maker->onTradeUpdate(&t, book.get());
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies order updates are forwarded to the order manager (ACCEPTED -> LIVE).
TEST_F(MarketMakerTest, OrderUpdateForwardedToOrderManager) {
  makeTwoSidedBook();
  refreshFeatures();
  drainOutReq();
  maker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  // Capture the actual BUY order id issued instead of assuming id 1.
  OrderId buyId = INVALID_ORDER_ID;
  for (auto *r = outReq->getNextToRead(); r; r = outReq->getNextToRead()) {
    if (r->m_side == common::Side::BUY)
      buyId = r->m_orderId;
    outReq->updateReadIndex();
  }
  ASSERT_NE(buyId, INVALID_ORDER_ID);

  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         buyId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  maker->onOrderUpdate(&acc);
  EXPECT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::LIVE);
}
