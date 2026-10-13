#include <gtest/gtest.h>

#include "common/logging.hpp"
#include "trading/strategy/position_keeper.hpp"

using namespace trading;
using namespace exchange;

class PositionKeeperTest : public ::testing::Test {
protected:
  static common::Logger *logger;

  PositionKeeper *keeper = nullptr;

  static void SetUpTestSuite() {
    logger = new common::Logger("test_position_keeper.log");
  }

  static void TearDownTestSuite() {
    delete logger;
    logger = nullptr;
  }

  void SetUp() override {
    keeper = new PositionKeeper(logger);
  }

  void TearDown() override {
    delete keeper;
    keeper = nullptr;
  }

  ClientResponseImpl fill(TickerId t, common::Side s, Price p, Quantity q) {
    return ClientResponseImpl{
        ClientResponseType::FILLED, 1, t, 7, 900, s, p, q, 0};
  }
};

common::Logger *PositionKeeperTest::logger = nullptr;

// Verifies a fresh keeper starts flat with zero PnL and volume.
TEST_F(PositionKeeperTest, StartsFlatWithZeroPnl) {
  const auto &pos = keeper->getPositionInfo(0);
  EXPECT_EQ(pos.m_position, 0);
  EXPECT_DOUBLE_EQ(pos.m_realPnL, 0.0);
  EXPECT_EQ(pos.m_volume, 0u);
}

// Verifies a BUY fill opens a long position with correct VWAP basis.
TEST_F(PositionKeeperTest, BuyFillOpensLong) {
  auto f = fill(0, common::Side::BUY, 100, 10);
  keeper->addFill(&f);
  const auto &pos = keeper->getPositionInfo(0);
  EXPECT_EQ(pos.m_position, 10);
  EXPECT_EQ(pos.m_volume, 10u);
}

// Verifies a SELL fill opens a short (negative) position.
TEST_F(PositionKeeperTest, SellFillOpensShort) {
  auto f = fill(0, common::Side::SELL, 100, 10);
  keeper->addFill(&f);
  EXPECT_EQ(keeper->getPositionInfo(0).m_position, -10);
}

// Verifies round-trip buy then sell flattens and resets VWAP state.
TEST_F(PositionKeeperTest, RoundTripFlattensPosition) {
  auto b = fill(0, common::Side::BUY, 100, 10);
  auto s = fill(0, common::Side::SELL, 110, 10);
  keeper->addFill(&b);
  keeper->addFill(&s);
  const auto &pos = keeper->getPositionInfo(0);
  EXPECT_EQ(pos.m_position, 0);
  EXPECT_GT(pos.m_realPnL, 0.0); // Bought 100, sold 110 -> profit 100.
}

// Verifies a losing round-trip produces negative realized PnL.
TEST_F(PositionKeeperTest, LosingRoundTripIsNegativePnl) {
  auto b = fill(0, common::Side::BUY, 110, 10);
  auto s = fill(0, common::Side::SELL, 100, 10);
  keeper->addFill(&b);
  keeper->addFill(&s);
  EXPECT_LT(keeper->getPositionInfo(0).m_realPnL, 0.0);
}

// Verifies partial close realizes PnL on the closed portion only.
TEST_F(PositionKeeperTest, PartialCloseRealizesProportionally) {
  auto b = fill(0, common::Side::BUY, 100, 10);
  auto s = fill(0, common::Side::SELL, 110, 4);
  keeper->addFill(&b);
  keeper->addFill(&s);
  const auto &pos = keeper->getPositionInfo(0);
  EXPECT_EQ(pos.m_position, 6);
  EXPECT_DOUBLE_EQ(pos.m_realPnL, 40.0); // 4 * (110 - 100).
}

// Verifies flipping from long to short carries the correct signed position.
TEST_F(PositionKeeperTest, FlipFromLongToShort) {
  auto b = fill(0, common::Side::BUY, 100, 5);
  auto s = fill(0, common::Side::SELL, 100, 10);
  keeper->addFill(&b);
  keeper->addFill(&s);
  EXPECT_EQ(keeper->getPositionInfo(0).m_position, -5);
}

// Verifies fills on different tickers are tracked independently.
TEST_F(PositionKeeperTest, TickersAreIndependent) {
  auto f0 = fill(0, common::Side::BUY, 100, 10);
  auto f1 = fill(1, common::Side::SELL, 100, 3);
  keeper->addFill(&f0);
  keeper->addFill(&f1);
  EXPECT_EQ(keeper->getPositionInfo(0).m_position, 10);
  EXPECT_EQ(keeper->getPositionInfo(1).m_position, -3);
}

// Verifies updateBBO recomputes unrealized PnL off the mid price.
TEST_F(PositionKeeperTest, BboUpdateRefreshesUnrealPnl) {
  auto b = fill(0, common::Side::BUY, 100, 10);
  keeper->addFill(&b);
  BBO bbo;
  bbo.m_bidPrice = 110;
  bbo.m_askPrice = 112;
  bbo.m_bidQty = 5;
  bbo.m_askQty = 5;
  keeper->updateBBO(0, &bbo);
  // Mid 111 vs VWAP 100 over 10 lots -> +110 unreal.
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_unrealPnL, 110.0);
}

// Verifies updateBBO with no position leaves PnL at zero.
TEST_F(PositionKeeperTest, BboUpdateWithFlatPositionIsNoOp) {
  BBO bbo;
  bbo.m_bidPrice = 110;
  bbo.m_askPrice = 112;
  bbo.m_bidQty = 5;
  bbo.m_askQty = 5;
  keeper->updateBBO(0, &bbo);
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_unrealPnL, 0.0);
}

// Verifies opening fills average the VWAP: buy 10@100 + 10@110 -> VWAP 105,
// then selling 10@120 realizes (120-105)*10 = 150.
TEST_F(PositionKeeperTest, VwapAveragesAcrossFills) {
  auto b1 = fill(0, common::Side::BUY, 100, 10);
  auto b2 = fill(0, common::Side::BUY, 110, 10);
  keeper->addFill(&b1);
  keeper->addFill(&b2);
  EXPECT_EQ(keeper->getPositionInfo(0).m_position, 20);
  auto s = fill(0, common::Side::SELL, 120, 10);
  keeper->addFill(&s);
  EXPECT_EQ(keeper->getPositionInfo(0).m_position, 10);
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_realPnL, 150.0);
}

// Verifies a fresh open has zero unrealized PnL before any BBO update
// (unreal is priced off the fill itself, so open is flat to market).
TEST_F(PositionKeeperTest, FreshOpenHasZeroUnreal) {
  auto b = fill(0, common::Side::BUY, 100, 10);
  keeper->addFill(&b);
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_unrealPnL, 0.0);
}

// Verifies flipping long->short realizes PnL on the closed leg and re-bases
// VWAP on the new side: buy 5@100, sell 10@110 -> +50 realized, short 5.
TEST_F(PositionKeeperTest, FlipRealizesClosedLegAndRebasesVwap) {
  auto b = fill(0, common::Side::BUY, 100, 5);
  auto s = fill(0, common::Side::SELL, 110, 10);
  keeper->addFill(&b);
  keeper->addFill(&s);
  const auto &pos = keeper->getPositionInfo(0);
  EXPECT_EQ(pos.m_position, -5);
  EXPECT_DOUBLE_EQ(pos.m_realPnL, 50.0);
}

// Verifies short-side unreal PnL: short 10@100, mid 90 -> +100 unreal.
TEST_F(PositionKeeperTest, ShortUnrealPnlFromBbo) {
  auto s = fill(0, common::Side::SELL, 100, 10);
  keeper->addFill(&s);
  BBO bbo;
  bbo.m_bidPrice = 89;
  bbo.m_askPrice = 91;
  bbo.m_bidQty = 5;
  bbo.m_askQty = 5;
  keeper->updateBBO(0, &bbo);
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_unrealPnL, 100.0);
}

// Verifies BBO with an invalid leg leaves unreal PnL untouched.
TEST_F(PositionKeeperTest, InvalidBboLegLeavesUnrealUntouched) {
  auto b = fill(0, common::Side::BUY, 100, 10);
  keeper->addFill(&b);
  BBO bbo;
  bbo.m_bidPrice = common::INVALID_PRICE;
  bbo.m_askPrice = 112;
  bbo.m_bidQty = 5;
  bbo.m_askQty = 5;
  keeper->updateBBO(0, &bbo);
  EXPECT_DOUBLE_EQ(keeper->getPositionInfo(0).m_unrealPnL, 0.0);
}
