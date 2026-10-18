#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "common/logging.hpp"
#include "exchange/market_data/market_update.hpp"
#include "trading/trader/market_order_book.hpp"
#include "trading/trader/trade_engine.hpp"

using namespace trading;
using namespace exchange;

// NOTE: prices are raw tick values (< ME_MAX_PRICE_LEVELS) so each maps to a
// distinct price-bucket via price % ME_MAX_PRICE_LEVELS (book logic).

class TradingBookTest : public ::testing::Test {
protected:
  static std::unique_ptr<ClientRequestImplQueue> outReq;
  static std::unique_ptr<ClientResponseImplQueue> inResp;
  static std::unique_ptr<MarketUpdateImplQueue> inMd;
  static std::unique_ptr<common::Logger> engLogger;
  static std::unique_ptr<TradeEngine> engine;
  static std::unique_ptr<common::Logger> bookLogger;
  // Retired books stay alive until the engine is destroyed, since the
  // engine's position keeper points at their BBOs and reads them on stop().
  static std::vector<std::unique_ptr<MarketOrderBook>> retired;

  std::unique_ptr<MarketOrderBook> book = nullptr;

  static void SetUpTestSuite() {
    outReq = std::make_unique<ClientRequestImplQueue>(4096);
    inResp = std::make_unique<ClientResponseImplQueue>(4096);
    inMd = std::make_unique<MarketUpdateImplQueue>(4096);
    engLogger = std::make_unique<common::Logger>("test_trading_book_eng.log");
    TradeEngineConfigMap cfg{};
    engine = std::make_unique<TradeEngine>(1, AlgoType::INVALID, cfg,
                                           outReq.get(), inResp.get(),
                                           inMd.get());
    bookLogger = std::make_unique<common::Logger>("test_trading_book.log");
  }

  static void TearDownTestSuite() {
    engine.reset();
    retired.clear();
    engLogger.reset();
    bookLogger.reset();
    outReq.reset();
    inResp.reset();
    inMd.reset();
  }

  void SetUp() override {
    book = std::make_unique<MarketOrderBook>(0, bookLogger.get());
    book->setTradeEngine(engine.get());
  }

  void TearDown() override {
    retired.push_back(std::move(book));
  }

  MarketUpdateImpl addUpd(OrderId oid, common::Side s, Price p, Quantity q,
                          Priority pr = 1) {
    return MarketUpdateImpl(MarketUpdateType::ADD, oid, 0, s, p, q, pr);
  }
};

std::unique_ptr<ClientRequestImplQueue> TradingBookTest::outReq = nullptr;
std::unique_ptr<ClientResponseImplQueue> TradingBookTest::inResp = nullptr;
std::unique_ptr<MarketUpdateImplQueue> TradingBookTest::inMd = nullptr;
std::unique_ptr<common::Logger> TradingBookTest::engLogger = nullptr;
std::unique_ptr<TradeEngine> TradingBookTest::engine = nullptr;
std::unique_ptr<common::Logger> TradingBookTest::bookLogger = nullptr;
std::vector<std::unique_ptr<MarketOrderBook>> TradingBookTest::retired{};

// Verifies the first update on an empty side leaves BBO invalid (book only
// refreshes BBO when the side already exists).
TEST_F(TradingBookTest, FirstBidLeavesBboInvalid) {
  auto u = addUpd(1, common::Side::BUY, 100, 10);
  book->onMarketUpdate(&u);
  EXPECT_EQ(book->getBBO()->m_bidPrice, common::INVALID_PRICE);
}

// Verifies the second bid update populates the bid BBO side.
TEST_F(TradingBookTest, SecondBidSetsBidBbo) {
  auto u1 = addUpd(1, common::Side::BUY, 100, 10);
  auto u2 = addUpd(2, common::Side::BUY, 100, 7);
  book->onMarketUpdate(&u1);
  book->onMarketUpdate(&u2);
  const BBO *bbo = book->getBBO();
  EXPECT_EQ(bbo->m_bidPrice, 100u);
  EXPECT_EQ(bbo->m_bidQty, 17u);
}

// Verifies the second ask update populates the ask BBO side.
TEST_F(TradingBookTest, SecondAskSetsAskBbo) {
  auto u1 = addUpd(1, common::Side::SELL, 101, 5);
  auto u2 = addUpd(2, common::Side::SELL, 101, 6);
  book->onMarketUpdate(&u1);
  book->onMarketUpdate(&u2);
  const BBO *bbo = book->getBBO();
  EXPECT_EQ(bbo->m_askPrice, 101u);
  EXPECT_EQ(bbo->m_askQty, 11u);
}

// Verifies BBO bid quantity aggregates multiple orders at the best price.
TEST_F(TradingBookTest, BidQtyAggregatesAtSamePrice) {
  auto u1 = addUpd(1, common::Side::BUY, 100, 10);
  auto u2 = addUpd(2, common::Side::BUY, 100, 7);
  book->onMarketUpdate(&u1);
  book->onMarketUpdate(&u2);
  EXPECT_EQ(book->getBBO()->m_bidQty, 17u);
}

// Verifies MODIFY changes the resting quantity and refreshes BBO.
TEST_F(TradingBookTest, ModifyUpdatesQtyAndBbo) {
  auto u = addUpd(1, common::Side::BUY, 100, 10);
  book->onMarketUpdate(&u);
  MarketUpdateImpl mod(
      MarketUpdateType::MODIFY, 1, 0, common::Side::BUY, 100, 4, 1);
  book->onMarketUpdate(&mod);
  EXPECT_EQ(book->getBBO()->m_bidQty, 4u);
}

// Verifies MODIFY only updates qty, not price (book logic): sending a MODIFY
// with a different price leaves the order at its original price level. Note
// the BBO is also stale here: the book only refreshes BBO when the update
// price >= best bid, so a MODIFY quoting 99 while best is 100 skips refresh.
TEST_F(TradingBookTest, ModifyIgnoresPriceChange) {
  auto u1 = addUpd(1, common::Side::BUY, 100, 10);
  auto u2 = addUpd(2, common::Side::BUY, 100, 7);
  book->onMarketUpdate(&u1);
  book->onMarketUpdate(&u2);
  ASSERT_EQ(book->getBBO()->m_bidPrice, 100u);
  ASSERT_EQ(book->getBBO()->m_bidQty, 17u);
  // MODIFY qty to 4 but claim price 99: stored qty updates, price level and
  // BBO do not (bidChanged = 99 >= 100 is false).
  MarketUpdateImpl mod(
      MarketUpdateType::MODIFY, 1, 0, common::Side::BUY, 99, 4, 1);
  book->onMarketUpdate(&mod);
  EXPECT_EQ(book->getBBO()->m_bidPrice, 100u);
  EXPECT_EQ(book->getBBO()->m_bidQty, 17u);
  // A same-price MODIFY does refresh and shows the new qty.
  MarketUpdateImpl mod2(
      MarketUpdateType::MODIFY, 1, 0, common::Side::BUY, 100, 4, 1);
  book->onMarketUpdate(&mod2);
  EXPECT_EQ(book->getBBO()->m_bidQty, 11u); // 4 + 7.
}

// Verifies CANCEL of the last order at a level clears that BBO side.
TEST_F(TradingBookTest, CancelLastOrderClearsLevel) {
  auto u = addUpd(1, common::Side::BUY, 100, 10);
  book->onMarketUpdate(&u);
  MarketUpdateImpl c(
      MarketUpdateType::CANCEL, 1, 0, common::Side::BUY, 100, 0, 1);
  book->onMarketUpdate(&c);
  EXPECT_EQ(book->getBBO()->m_bidPrice, common::INVALID_PRICE);
}

// Verifies CLEAR drops old orders so the book rebuilds cleanly from later ADDs.
TEST_F(TradingBookTest, ClearDropsOldOrdersAndRebuilds) {
  auto b = addUpd(1, common::Side::BUY, 100, 10);
  auto a = addUpd(2, common::Side::SELL, 101, 5);
  book->onMarketUpdate(&b);
  book->onMarketUpdate(&a);
  MarketUpdateImpl c(MarketUpdateType::CLEAR,
                     0,
                     0,
                     common::Side::INVALID,
                     common::INVALID_PRICE,
                     0,
                     common::INVALID_PRIORITY);
  book->onMarketUpdate(&c);
  // Rebuild with two bids; the second refreshes BBO to the new level.
  auto n1 = addUpd(3, common::Side::BUY, 99, 7);
  auto n2 = addUpd(4, common::Side::BUY, 99, 3);
  book->onMarketUpdate(&n1);
  book->onMarketUpdate(&n2);
  EXPECT_EQ(book->getBBO()->m_bidPrice, 99u);
  EXPECT_EQ(book->getBBO()->m_bidQty, 10u);
  // Ask side was cleared too.
  EXPECT_EQ(book->getBBO()->m_askPrice, common::INVALID_PRICE);
}

// Verifies TRADE updates do not mutate the book and skip order-book callbacks.
TEST_F(TradingBookTest, TradeDoesNotMutateBook) {
  auto b1 = addUpd(1, common::Side::BUY, 100, 10);
  auto b2 = addUpd(2, common::Side::BUY, 100, 7);
  book->onMarketUpdate(&b1);
  book->onMarketUpdate(&b2);
  ASSERT_EQ(book->getBBO()->m_bidQty, 17u);
  MarketUpdateImpl t(MarketUpdateType::TRADE,
                     common::INVALID_ORDER_ID,
                     0,
                     common::Side::BUY,
                     100,
                     3,
                     common::INVALID_PRIORITY);
  book->onMarketUpdate(&t);
  EXPECT_EQ(book->getBBO()->m_bidQty, 17u);
}

// Verifies the better (higher) bid becomes BBO when two levels exist.
TEST_F(TradingBookTest, BetterBidWinsBbo) {
  auto low = addUpd(1, common::Side::BUY, 99, 10);
  auto high = addUpd(2, common::Side::BUY, 100, 5);
  book->onMarketUpdate(&low);
  book->onMarketUpdate(&high);
  EXPECT_EQ(book->getBBO()->m_bidPrice, 100u);
}

// Verifies the better (lower) ask becomes BBO when two levels exist.
TEST_F(TradingBookTest, BetterAskWinsBbo) {
  auto high = addUpd(1, common::Side::SELL, 102, 10);
  auto low = addUpd(2, common::Side::SELL, 101, 5);
  book->onMarketUpdate(&high);
  book->onMarketUpdate(&low);
  EXPECT_EQ(book->getBBO()->m_askPrice, 101u);
}

// Verifies removing the best level promotes the next level to BBO.
TEST_F(TradingBookTest, CancelBestPromotesNextLevel) {
  auto low = addUpd(1, common::Side::BUY, 99, 10);
  auto high = addUpd(2, common::Side::BUY, 100, 5);
  book->onMarketUpdate(&low);
  book->onMarketUpdate(&high);
  MarketUpdateImpl c(
      MarketUpdateType::CANCEL, 2, 0, common::Side::BUY, 100, 0, 2);
  book->onMarketUpdate(&c);
  EXPECT_EQ(book->getBBO()->m_bidPrice, 99u);
}

// Verifies adding at a worse price does not move the BBO.
TEST_F(TradingBookTest, WorsePriceAddLeavesBboUnmoved) {
  auto best = addUpd(1, common::Side::BUY, 100, 10);
  auto best2 = addUpd(2, common::Side::BUY, 100, 5);
  book->onMarketUpdate(&best);
  book->onMarketUpdate(&best2);
  ASSERT_EQ(book->getBBO()->m_bidPrice, 100u);
  ASSERT_EQ(book->getBBO()->m_bidQty, 15u);
  auto worse = addUpd(3, common::Side::BUY, 90, 100);
  book->onMarketUpdate(&worse);
  EXPECT_EQ(book->getBBO()->m_bidPrice, 100u);
  EXPECT_EQ(book->getBBO()->m_bidQty, 15u);
}
