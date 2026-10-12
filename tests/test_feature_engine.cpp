#include <cmath>
#include <gtest/gtest.h>

#include <vector>

#include "common/logging.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/market_order_book.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class FeatureEngineTest : public ::testing::Test {
protected:
  static common::Logger *engLogger;
  static TradeEngine *engine;
  static common::Logger *bookLogger;
  static common::Logger *featLogger;
  // Retired books stay alive until the engine is destroyed, since the
  // engine's position keeper points at their BBOs and reads them on stop().
  static std::vector<MarketOrderBook *> retired;

  FeatureEngine *features = nullptr;
  MarketOrderBook *book = nullptr;

  static void SetUpTestSuite() {
    ClientRequestQueue *rq = new ClientRequestQueue(1024);
    ClientResponseQueue *rs = new ClientResponseQueue(1024);
    MarketUpdateQueue *md = new MarketUpdateQueue(1024);
    engLogger = new common::Logger("test_feature_eng.log");
    TradeEngineConfigMap cfg{};
    // Leaked queues are fine for unit tests; engine is torn down once.
    engine = new TradeEngine(1, AlgoType::INVALID, cfg, rq, rs, md);
    bookLogger = new common::Logger("test_feature_book.log");
    featLogger = new common::Logger("test_feature.log");
  }

  static void TearDownTestSuite() {
    delete engine;
    engine = nullptr;
    for (auto *b : retired) {
      delete b;
    }
    retired.clear();
    delete engLogger;
    delete bookLogger;
    delete featLogger;
  }

  void SetUp() override {
    features = new FeatureEngine(featLogger);
    book = new MarketOrderBook(0, bookLogger);
    book->setTradeEngine(engine);
  }

  void TearDown() override {
    retired.push_back(book);
    book = nullptr;
    delete features;
    features = nullptr;
  }

  void addToBook(OrderId id, common::Side s, Price p, Quantity q) {
    MEMarketUpdate u{MarketUpdateType::ADD, id, 0, s, p, q, 1};
    book->onMarketUpdate(&u);
  }
};

common::Logger *FeatureEngineTest::engLogger = nullptr;
TradeEngine *FeatureEngineTest::engine = nullptr;
common::Logger *FeatureEngineTest::bookLogger = nullptr;
common::Logger *FeatureEngineTest::featLogger = nullptr;
std::vector<MarketOrderBook *> FeatureEngineTest::retired{};

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
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  // (100*20 + 102*20) / 40 = 101.
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
}

// Verifies micro-price skews toward the thinner side of the book.
TEST_F(FeatureEngineTest, MicroPriceSkewsToThinSide) {
  addToBook(1, common::Side::BUY, 100, 30);
  addToBook(2, common::Side::BUY, 100, 10); // Bid BBO qty now 40.
  addToBook(3, common::Side::SELL, 110, 10);
  addToBook(4, common::Side::SELL, 110, 10); // Ask BBO qty now 20.
  features->onOrderBookUpdate(0, 110, common::Side::SELL, book);
  double expected = (100.0 * 20 + 110.0 * 40) / 60.0;
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), expected);
}

// Verifies one-sided books leave the micro-price invalid.
TEST_F(FeatureEngineTest, OneSidedBookKeepsPriceInvalid) {
  addToBook(1, common::Side::BUY, 100, 10);
  features->onOrderBookUpdate(0, 100, common::Side::BUY, book);
  EXPECT_TRUE(std::isnan(features->getMarketPrice()));
}

// Verifies a valid micro-price goes stale (not reset) when the book becomes
// one-sided: book logic only overwrites on two-sided BBO.
TEST_F(FeatureEngineTest, StalePriceRetainedWhenBookBecomesOneSided) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10);
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  ASSERT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
  // Remove the entire ask side: BBO ask goes invalid, feature must keep 101.
  MEMarketUpdate c1{MarketUpdateType::CANCEL, 3, 0, common::Side::SELL,
                    102, 0, 1};
  MEMarketUpdate c2{MarketUpdateType::CANCEL, 4, 0, common::Side::SELL,
                    102, 0, 1};
  book->onMarketUpdate(&c1);
  book->onMarketUpdate(&c2);
  ASSERT_EQ(book->getBBO()->m_askPrice, common::INVALID_PRICE);
  features->onOrderBookUpdate(0, 102, common::Side::SELL, book);
  EXPECT_DOUBLE_EQ(features->getMarketPrice(), 101.0);
}

// Verifies an aggressive BUY ratio is sized against the ask quantity.
TEST_F(FeatureEngineTest, AggressiveBuyRatioUsesAskQty) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10); // Ask BBO qty now 20.
  MEMarketUpdate t{MarketUpdateType::TRADE, common::INVALID_ORDER_ID, 0,
                   common::Side::BUY, 102, 10,
                   common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book);
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 0.5);
}

// Verifies an aggressive SELL ratio is sized against the bid quantity.
TEST_F(FeatureEngineTest, AggressiveSellRatioUsesBidQty) {
  addToBook(1, common::Side::BUY, 100, 20);
  addToBook(2, common::Side::BUY, 100, 20); // Bid BBO qty now 40.
  addToBook(3, common::Side::SELL, 102, 10);
  addToBook(4, common::Side::SELL, 102, 10);
  MEMarketUpdate t{MarketUpdateType::TRADE, common::INVALID_ORDER_ID, 0,
                   common::Side::SELL, 100, 10,
                   common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book);
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 0.25);
}

// Verifies trade updates without a two-sided BBO leave the ratio invalid.
TEST_F(FeatureEngineTest, TradeWithoutBboKeepsRatioInvalid) {
  MEMarketUpdate t{MarketUpdateType::TRADE, common::INVALID_ORDER_ID, 0,
                   common::Side::BUY, 100, 5,
                   common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book);
  EXPECT_TRUE(std::isnan(features->getAggresiveTradeQtyRatio()));
}

// Verifies an aggressive ratio above 1 is possible when the trade out-sizes
// the BBO (book divides trade qty by resting qty without clamping).
TEST_F(FeatureEngineTest, RatioAboveOneWhenTradeOutsizesBbo) {
  addToBook(1, common::Side::BUY, 100, 10);
  addToBook(2, common::Side::BUY, 100, 10);
  addToBook(3, common::Side::SELL, 102, 5);
  addToBook(4, common::Side::SELL, 102, 5); // Ask BBO qty now 10.
  MEMarketUpdate t{MarketUpdateType::TRADE, common::INVALID_ORDER_ID, 0,
                   common::Side::BUY, 102, 30,
                   common::INVALID_PRIORITY};
  features->onTradeUpdate(&t, book);
  EXPECT_DOUBLE_EQ(features->getAggresiveTradeQtyRatio(), 3.0);
}
