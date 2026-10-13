#include <gtest/gtest.h>

#include <memory>

#include "common/logging.hpp"
#include "trading/strategy/liquidity_taker.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class LiquidityTakerTest : public ::testing::Test {
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
  std::unique_ptr<LiquidityTaker> taker = nullptr;
  std::unique_ptr<MarketOrderBook> book = nullptr;
  TradeEngineConfigMap cfg{};

  static void SetUpTestSuite() {
    outReq = std::make_unique<ClientRequestImplQueue>(4096);
    inResp = std::make_unique<ClientResponseImplQueue>(4096);
    inMd = std::make_unique<MarketUpdateImplQueue>(4096);
    logger = std::make_unique<common::Logger>("test_liquidity_taker.log");
  }

  static void TearDownTestSuite() {
    logger.reset();
    outReq.reset();
    inResp.reset();
    inMd.reset();
  }

  void SetUp() override {
    while (outReq->size())
      outReq->updateReadIndex();
    cfg = TradeEngineConfigMap{};
    cfg[0].m_clip = 10;
    cfg[0].m_threshold = 0.5;
    cfg[0].m_riskConfig.m_maxOrderSize = 100;
    cfg[0].m_riskConfig.m_maxPosition = 1000;
    cfg[0].m_riskConfig.m_maxLoss = -1e9;

    engine = std::make_unique<TradeEngine>(7, AlgoType::INVALID, cfg,
                                           outReq.get(), inResp.get(),
                                           inMd.get());
    features = std::make_unique<FeatureEngine>(logger.get());
    keeper = std::make_unique<PositionKeeper>(logger.get());
    risk = std::make_unique<RiskManager>(keeper.get(), cfg, logger.get());
    oms = std::make_unique<OrderManager>(engine.get(), *risk, logger.get());
    taker = std::make_unique<LiquidityTaker>(engine.get(), features.get(),
                                             oms.get(), cfg, logger.get());
    book = std::make_unique<MarketOrderBook>(0, logger.get());
    book->setTradeEngine(engine.get());
  }

  void TearDown() override {
    // Destroy the engine before the book: the engine's position keeper holds
    // a pointer to this book's BBO and dereferences it while stopping.
    taker.reset();
    oms.reset();
    risk.reset();
    keeper.reset();
    features.reset();
    engine.reset();
    book.reset();
    while (outReq->size())
      outReq->updateReadIndex();
    while (inResp->size())
      inResp->updateReadIndex();
    while (inMd->size())
      inMd->updateReadIndex();
  }

  // Builds a two-sided book with valid BBO (two updates per side, since the
  // book only refreshes BBO once the side already exists).
  void makeTwoSidedBook() {
    MarketUpdateImpl b1{
        MarketUpdateType::ADD, 1, 0, common::Side::BUY, 100, 10, 1};
    MarketUpdateImpl b2{
        MarketUpdateType::ADD, 2, 0, common::Side::BUY, 100, 10, 1};
    MarketUpdateImpl a1{
        MarketUpdateType::ADD, 3, 0, common::Side::SELL, 102, 10, 1};
    MarketUpdateImpl a2{
        MarketUpdateType::ADD, 4, 0, common::Side::SELL, 102, 10, 1};
    book->onMarketUpdate(&b1);
    book->onMarketUpdate(&b2);
    book->onMarketUpdate(&a1);
    book->onMarketUpdate(&a2);
  }
};

std::unique_ptr<ClientRequestImplQueue> LiquidityTakerTest::outReq = nullptr;
std::unique_ptr<ClientResponseImplQueue> LiquidityTakerTest::inResp = nullptr;
std::unique_ptr<MarketUpdateImplQueue> LiquidityTakerTest::inMd = nullptr;
std::unique_ptr<common::Logger> LiquidityTakerTest::logger = nullptr;

// Verifies order-book updates alone never trigger taker orders.
TEST_F(LiquidityTakerTest, BookUpdateSendsNothing) {
  makeTwoSidedBook();
  taker->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies a weak (below-threshold) aggressive ratio sends nothing.
TEST_F(LiquidityTakerTest, WeakSignalSendsNothing) {
  makeTwoSidedBook();
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     1,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get()); // Ratio 1/20 = 0.05 < 0.5.
  taker->onTradeUpdate(&t, book.get());
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies a ratio exactly at threshold still triggers (book uses >=).
TEST_F(LiquidityTakerTest, ThresholdBoundaryTriggers) {
  makeTwoSidedBook(); // ask BBO qty 20.
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     10,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get()); // Ratio 10/20 = 0.5 == threshold.
  ASSERT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 0.5);
  taker->onTradeUpdate(&t, book.get());
  EXPECT_EQ(outReq->size(), 1u);
}

// Verifies a strong aggressive BUY triggers exactly one buy at the ask with
// clip qty, and no bid leg (opposite leg is INVALID).
TEST_F(LiquidityTakerTest, StrongBuySignalBuysAtAsk) {
  makeTwoSidedBook();
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     20,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get()); // Ratio 20/20 = 1.0 >= 0.5.
  taker->onTradeUpdate(&t, book.get());
  ASSERT_EQ(outReq->size(), 1u);
  const auto req = *outReq->getNextToRead();
  EXPECT_EQ(req.m_side, common::Side::BUY);
  EXPECT_EQ(req.m_price, 102u);
  EXPECT_EQ(req.m_quantity, 10u);
}

// Verifies a strong aggressive SELL triggers exactly one sell at the bid.
TEST_F(LiquidityTakerTest, StrongSellSignalSellsAtBid) {
  makeTwoSidedBook();
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::SELL,
                     100,
                     20,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get()); // Ratio 20/20 = 1.0 >= 0.5.
  taker->onTradeUpdate(&t, book.get());
  ASSERT_EQ(outReq->size(), 1u);
  const auto req = *outReq->getNextToRead();
  EXPECT_EQ(req.m_side, common::Side::SELL);
  EXPECT_EQ(req.m_price, 100u);
  EXPECT_EQ(req.m_quantity, 10u);
}

// Verifies an invalid (NaN) aggressive ratio sends nothing.
TEST_F(LiquidityTakerTest, InvalidRatioSendsNothing) {
  makeTwoSidedBook();
  // No feature update, so ratio stays NaN.
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     10,
                     common::INVALID_PRIORITY};
  taker->onTradeUpdate(&t, book.get());
  EXPECT_EQ(outReq->size(), 0u);
}

// Verifies order updates forward to the order manager (ACCEPTED -> LIVE)
// while sending no new requests themselves.
TEST_F(LiquidityTakerTest, OrderUpdateForwardedWithoutNewRequests) {
  makeTwoSidedBook();
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     20,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get());
  taker->onTradeUpdate(&t, book.get());
  ASSERT_EQ(outReq->size(), 1u);
  const OrderId buyId = outReq->getNextToRead()->m_orderId;
  while (outReq->size())
    outReq->updateReadIndex();

  ClientResponseImpl acc{ClientResponseType::ACCEPTED,
                         7,
                         0,
                         buyId,
                         500,
                         common::Side::BUY,
                         102,
                         0,
                         10};
  taker->onOrderUpdate(&acc);
  EXPECT_EQ(outReq->size(), 0u);
  EXPECT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::LIVE);
}
