#include <cmath>
#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "common/logging.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/market_order_book.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class FeatureEngineTest : public ::testing::Test {
protected:
  static std::unique_ptr<common::Logger> engLogger;
  static std::unique_ptr<TradeEngine> engine;
  static std::unique_ptr<common::Logger> bookLogger;
  static std::unique_ptr<common::Logger> featLogger;
  static std::unique_ptr<ClientRequestImplQueue> rq;
  static std::unique_ptr<ClientResponseImplQueue> rs;
  static std::unique_ptr<MarketUpdateImplQueue> md;
  // Retired books stay alive until the engine is destroyed, since the
  // engine's position keeper points at their BBOs and reads them on stop().
  static std::vector<std::unique_ptr<MarketOrderBook>> retired;

  std::unique_ptr<FeatureEngine> features = nullptr;
  std::unique_ptr<MarketOrderBook> book = nullptr;

  static void SetUpTestSuite() {
    rq = std::make_unique<ClientRequestImplQueue>(1024);
    rs = std::make_unique<ClientResponseImplQueue>(1024);
    md = std::make_unique<MarketUpdateImplQueue>(1024);
    engLogger = std::make_unique<common::Logger>("test_feature_eng.log");
    TradeEngineConfigMap cfg{};
    engine = std::make_unique<TradeEngine>(1, AlgoType::INVALID, cfg, rq.get(),
                                           rs.get(), md.get());
    bookLogger = std::make_unique<common::Logger>("test_feature_book.log");
    featLogger = std::make_unique<common::Logger>("test_feature.log");
  }

  static void TearDownTestSuite() {
    engine.reset();
    retired.clear();
    engLogger.reset();
    bookLogger.reset();
    featLogger.reset();
  }

  void SetUp() override {
    features = std::make_unique<FeatureEngine>(featLogger.get());
    book = std::make_unique<MarketOrderBook>(0, bookLogger.get());
    book->setTradeEngine(engine.get());
  }

  void TearDown() override {
    retired.push_back(std::move(book));
    features.reset();
  }

  void addToBook(OrderId id, common::Side s, Price p, Quantity q) {
    MarketUpdateImpl u{MarketUpdateType::ADD, id, 0, s, p, q, 1};
    book->onMarketUpdate(&u);
  }
};

std::unique_ptr<common::Logger> FeatureEngineTest::engLogger = nullptr;
std::unique_ptr<TradeEngine> FeatureEngineTest::engine = nullptr;
std::unique_ptr<common::Logger> FeatureEngineTest::bookLogger = nullptr;
std::unique_ptr<common::Logger> FeatureEngineTest::featLogger = nullptr;
std::unique_ptr<ClientRequestImplQueue> FeatureEngineTest::rq = nullptr;
std::unique_ptr<ClientResponseImplQueue> FeatureEngineTest::rs = nullptr;
std::unique_ptr<MarketUpdateImplQueue> FeatureEngineTest::md = nullptr;
std::vector<std::unique_ptr<MarketOrderBook>> FeatureEngineTest::retired{};

// Verifies the feature starts as NaN before any book update.
TEST_F(FeatureEngineTest, StartsInvalid) {
  EXPECT_TRUE(std::isnan(features->getMarketPrice()));
  EXPECT_TRUE(std::isnan(features->getAggresiveTradeQtyRatio()));
}

// Verifies micro-price is the quantity-weighted mid of the BBO.
TEST_F(FeatureEngineTest, MicroPriceIsQtyWeightedMid) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10);
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  // (100*20 + 102*20) / 40 = 101.
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
}

// Verifies micro-price skews toward the thinner side of the book.
TEST_F(FeatureEngineTest, MicroPriceSkewsToThinSide) {
  addToBook(1, common::Side::BUY, 100, 30);
  addToBook(2, common::Side::BUY, 100, 10); // Bid BBO qty now 40.
  addToBook(3, common::Side::SELL, 110, 10);
  addToBook(4, common::Side::SELL, 110, 10); // Ask BBO qty now 20.
  features->onOrderBookUpdate(0, 110, common::Side::SELL, book.get());
  double expected = (100.0 * 20 + 110.0 * 40) / 60.0;
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), expected);
}

// Verifies one-sided books leave the micro-price invalid.
TEST_F(FeatureEngineTest, OneSidedBookKeepsPriceInvalid) {
  addToBook(1, common::Side::BUY, 100, 10);
  features->onOrderBookUpdate(0, 100, common::Side::BUY, book.get());
  EXPECT_TRUE(std::isnan(features->getMarketPrice()));
}

// Verifies a valid micro-price goes stale (not reset) when the book becomes
// one-sided: book logic only overwrites on two-sided BBO.
TEST_F(FeatureEngineTest, StalePriceRetainedWhenBookBecomesOneSided) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10);
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  ASSERT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
  // Remove the entire ask side: BBO ask goes invalid, feature must keep 101.
  MarketUpdateImpl c1{
      MarketUpdateType::CANCEL, 3, 0, common::Side::SELL, 102, 0, 1};
  MarketUpdateImpl c2{
      MarketUpdateType::CANCEL, 4, 0, common::Side::SELL, 102, 0, 1};
  book->onMarketUpdate(&c1);
  book->onMarketUpdate(&c2);
  ASSERT_EQ(book->getBBO()->m_askPrice, common::INVALID_PRICE);
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book.get());
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
}

// Verifies an aggressive BUY ratio is sized against the ask quantity.
TEST_F(FeatureEngineTest, AggressiveBuyRatioUsesAskQty) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10); // Ask BBO qty now 20.
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     10,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get());
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 0.5);
}

// Verifies an aggressive SELL ratio is sized against the bid quantity.
TEST_F(FeatureEngineTest, AggressiveSellRatioUsesBidQty) {
  addToBook(1, common::Side::BUY, 100, 20);
  addToBook(2, common::Side::BUY, 100, 20); // Bid BBO qty now 40.
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10);
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::SELL,
                     100,
                     10,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get());
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 0.25);
}

// Verifies trade updates without a two-sided BBO leave the ratio invalid.
TEST_F(FeatureEngineTest, TradeWithoutBboKeepsRatioInvalid) {
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     100,
                     5,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get());
  EXPECT_TRUE(std::isnan(features->getAggresiveTradeQtyRatio()));
}

// Verifies an aggressive ratio above 1 is possible when the trade out-sizes
// the BBO (book divides trade qty by resting qty without clamping).
TEST_F(FeatureEngineTest, RatioAboveOneWhenTradeOutsizesBbo) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 5);
  addToBook(4, common::Side::SELL, 102, 5); // Ask BBO qty now 10.
  MarketUpdateImpl t{MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     102,
                     30,
                     common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book.get());
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 3.0);
}
