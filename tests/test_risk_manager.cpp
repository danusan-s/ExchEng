#include <gtest/gtest.h>
#include <memory>

#include "common/logging.hpp"
#include "trading/trader/position_keeper.hpp"
#include "trading/trader/risk_manager.hpp"

using namespace trading;
using namespace exchange;

class RiskManagerTest : public ::testing::Test {
protected:
  static std::unique_ptr<common::Logger> logger;

  std::unique_ptr<PositionKeeper> keeper = nullptr;
  std::unique_ptr<RiskManager> risk = nullptr;

  static void SetUpTestSuite() {
    logger = std::make_unique<common::Logger>("test_risk_manager.log");
  }

  static void TearDownTestSuite() {
    logger.reset();
  }

  void SetUp() override {
    keeper = std::make_unique<PositionKeeper>(logger.get());
    TradeEngineConfigMap cfg{};
    cfg.at(0).m_riskConfig.m_maxOrderSize = 100;
    cfg.at(0).m_riskConfig.m_maxPosition = 200;
    cfg.at(0).m_riskConfig.m_maxLoss = -1000.0;
    risk = std::make_unique<RiskManager>(keeper.get(), cfg, logger.get());
  }

  void TearDown() override {
    risk.reset();
    keeper.reset();
  }

  void buyFill(TickerId t, Price p, Quantity q) {
    ClientResponseImpl f{ClientResponseType::FILLED, 1, t, 7, 900,
                         common::Side::BUY,          p, q, 0};
    keeper->addFill(&f);
  }
};

std::unique_ptr<common::Logger> RiskManagerTest::logger = nullptr;

// Verifies a normal small order passes all risk checks.
TEST_F(RiskManagerTest, SmallOrderIsAllowed) {
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 10),
            RiskCheckResult::ALLOWED);
}

// Verifies an order larger than max order size is rejected.
TEST_F(RiskManagerTest, OrderTooLargeIsRejected) {
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 101),
            RiskCheckResult::ORDER_TOO_LARGE);
}

// Verifies an order exactly at max size is still allowed (boundary).
TEST_F(RiskManagerTest, OrderAtMaxSizeBoundaryIsAllowed) {
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 100),
            RiskCheckResult::ALLOWED);
}

// Verifies a new order that would breach max position is rejected.
TEST_F(RiskManagerTest, PositionBreachIsRejected) {
  buyFill(0, 100, 100);
  buyFill(0, 100, 90); // Position now 190.
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 20),
            RiskCheckResult::POSITION_TOO_LARGE);
}

// Verifies reducing (opposite side) orders pass even near the limit.
TEST_F(RiskManagerTest, ReducingOrderPassesPositionCheck) {
  buyFill(0, 100, 190);
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::SELL, 50),
            RiskCheckResult::ALLOWED);
}

// Verifies short-side position breaches are rejected symmetrically.
TEST_F(RiskManagerTest, ShortPositionBreachIsRejected) {
  ClientResponseImpl f{ClientResponseType::FILLED, 1,   0,   7, 900,
                       common::Side::SELL,         100, 190, 0};
  keeper->addFill(&f);
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::SELL, 20),
            RiskCheckResult::POSITION_TOO_LARGE);
}

// Verifies trading halts once total PnL drops below max loss.
TEST_F(RiskManagerTest, LossBreachHaltsTrading) {
  // Lose money: buy high, sell low for a -2000 realized loss.
  buyFill(0, 200, 20);
  ClientResponseImpl s{ClientResponseType::FILLED, 1,   0,  8, 901,
                       common::Side::SELL,         100, 20, 0};
  keeper->addFill(&s);
  ASSERT_LT(keeper->getPositionInfo(0).m_totalPnL, -1000.0);
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 10),
            RiskCheckResult::LOSS_TOO_LARGE);
}

// Verifies risk configs are per-ticker (default zero config rejects
// everything).
TEST_F(RiskManagerTest, UnconfiguredTickerRejectsAnyOrder) {
  EXPECT_EQ(risk->checkPreTradeRisk(3, common::Side::BUY, 1),
            RiskCheckResult::ORDER_TOO_LARGE);
}

// Verifies check order: oversized order reports ORDER_TOO_LARGE even when it
// would also breach the position limit.
TEST_F(RiskManagerTest, OrderSizeCheckWinsOverPositionCheck) {
  buyFill(0, 100, 190);
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 500),
            RiskCheckResult::ORDER_TOO_LARGE);
}

// Verifies zero-qty orders pass size/position checks when flat.
TEST_F(RiskManagerTest, ZeroQtyIsAllowedWhenFlat) {
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 0),
            RiskCheckResult::ALLOWED);
}

// Verifies the loss gate uses total PnL (real + unreal): open + BBO-driven
// unreal loss below maxLoss halts trading even with zero realized loss.
TEST_F(RiskManagerTest, UnrealLossHaltsTrading) {
  buyFill(0, 100, 20); // long 20 @ 100.
  BBO bbo;
  bbo.m_bidPrice = 10;
  bbo.m_askPrice = 10;
  bbo.m_bidQty = 5;
  bbo.m_askQty = 5;
  keeper->updateBBO(0, &bbo); // mid 10 -> unreal (10-100)*20 = -1800.
  ASSERT_LT(keeper->getPositionInfo(0).m_totalPnL, -1000.0);
  EXPECT_EQ(keeper->getPositionInfo(0).m_realPnL, 0.0);
  EXPECT_EQ(risk->checkPreTradeRisk(0, common::Side::BUY, 10),
            RiskCheckResult::LOSS_TOO_LARGE);
}
