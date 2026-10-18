#include <gtest/gtest.h>
#include <memory>

#include "common/logging.hpp"
#include "trading/trader/order_manager.hpp"
#include "trading/trader/trade_engine.hpp"

using namespace trading;
using namespace exchange;

class OrderManagerTest : public ::testing::Test {
protected:
  static std::unique_ptr<ClientRequestImplQueue> outReq;
  static std::unique_ptr<ClientResponseImplQueue> inResp;
  static std::unique_ptr<MarketUpdateImplQueue> inMd;
  static std::unique_ptr<common::Logger> engLogger;
  static std::unique_ptr<TradeEngine> engine;
  static std::unique_ptr<common::Logger> omLogger;

  std::unique_ptr<PositionKeeper> keeper = nullptr;
  std::unique_ptr<RiskManager> risk = nullptr;
  std::unique_ptr<OrderManager> oms = nullptr;

  static void SetUpTestSuite() {
    outReq = std::make_unique<ClientRequestImplQueue>(4096);
    inResp = std::make_unique<ClientResponseImplQueue>(4096);
    inMd = std::make_unique<MarketUpdateImplQueue>(4096);
    engLogger = std::make_unique<common::Logger>("test_om_eng.log");
    TradeEngineConfigMap cfg{};
    for (auto &c : cfg) {
      c.m_clip = 10;
      c.m_riskConfig.m_maxOrderSize = 100;
      c.m_riskConfig.m_maxPosition = 1000;
      c.m_riskConfig.m_maxLoss = -1e9;
    }
    engine = std::make_unique<TradeEngine>(7, AlgoType::INVALID, cfg,
                                           outReq.get(), inResp.get(),
                                           inMd.get());
    omLogger = std::make_unique<common::Logger>("test_order_manager.log");
  }

  static void TearDownTestSuite() {
    engine.reset();
    engLogger.reset();
    omLogger.reset();
  }

  void SetUp() override {
    while (outReq->getNextToRead() != nullptr)
      outReq->updateReadIndex();
    keeper = std::make_unique<PositionKeeper>(omLogger.get());
    TradeEngineConfigMap cfg{};
    for (auto &c : cfg) {
      c.m_riskConfig.m_maxOrderSize = 100;
      c.m_riskConfig.m_maxPosition = 1000;
      c.m_riskConfig.m_maxLoss = -1e9;
    }
    risk = std::make_unique<RiskManager>(keeper.get(), cfg, omLogger.get());
    oms = std::make_unique<OrderManager>(engine.get(), *risk, omLogger.get());
  }

  void TearDown() override {
    oms.reset();
    risk.reset();
    keeper.reset();
    while (outReq->getNextToRead() != nullptr)
      outReq->updateReadIndex();
  }

  ClientRequestImpl lastRequest() {
    EXPECT_NE(outReq->getNextToRead(), nullptr);
    return *outReq->getNextToRead();
  }
};

std::unique_ptr<ClientRequestImplQueue> OrderManagerTest::outReq = nullptr;
std::unique_ptr<ClientResponseImplQueue> OrderManagerTest::inResp = nullptr;
std::unique_ptr<MarketUpdateImplQueue> OrderManagerTest::inMd = nullptr;
std::unique_ptr<common::Logger> OrderManagerTest::engLogger = nullptr;
std::unique_ptr<TradeEngine> OrderManagerTest::engine = nullptr;
std::unique_ptr<common::Logger> OrderManagerTest::omLogger = nullptr;

// Verifies newOrder sends a NEW request and marks the slot PENDING_NEW.
TEST_F(OrderManagerTest, NewOrderSendsNewAndPends) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  EXPECT_EQ(order.m_state, OMOrderState::PENDING_NEW);
  EXPECT_EQ(order.m_price, 100u);
  auto req = lastRequest();
  EXPECT_EQ(req.m_type, ClientRequestType::NEW);
  EXPECT_EQ(req.m_side, common::Side::BUY);
}

// Verifies cancelOrder sends CANCEL and marks the slot PENDING_CANCEL.
TEST_F(OrderManagerTest, CancelOrderSendsCancelAndPends) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::LIVE};
  oms->cancelOrder(&order);
  EXPECT_EQ(order.m_state, OMOrderState::PENDING_CANCEL);
  auto req = lastRequest();
  EXPECT_EQ(req.m_type, ClientRequestType::CANCEL);
  EXPECT_EQ(req.m_orderId, 1u);
}

// Verifies ACCEPTED transitions the tracked order to LIVE.
TEST_F(OrderManagerTest, AcceptedMakesOrderLive) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&acc);
  EXPECT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::LIVE);
}

// Verifies CANCELED transitions the tracked order to DEAD.
TEST_F(OrderManagerTest, CanceledMakesOrderDead) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&acc);
  ClientResponseImpl cx(ClientResponseType::CANCELED,
                        7,
                        0,
                        order.m_orderId,
                        500,
                        common::Side::BUY,
                        100,
                        0,
                        10);
  oms->onOrderUpdate(&cx);
  EXPECT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::DEAD);
}

// Verifies a full FILLED (zero leaves) transitions the order to DEAD.
TEST_F(OrderManagerTest, FullFillMakesOrderDead) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&acc);
  ClientResponseImpl fl(ClientResponseType::FILLED,
                        7,
                        0,
                        order.m_orderId,
                        500,
                        common::Side::BUY,
                        100,
                        10,
                        0);
  oms->onOrderUpdate(&fl);
  const auto &tracked =
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY));
  EXPECT_EQ(tracked.m_state, OMOrderState::DEAD);
  EXPECT_EQ(tracked.m_qty, 0u);
}

// Verifies a partial FILLED updates leaves qty but keeps the order working.
TEST_F(OrderManagerTest, PartialFillKeepsOrderWorking) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&acc);
  ClientResponseImpl fl(ClientResponseType::FILLED,
                        7,
                        0,
                        order.m_orderId,
                        500,
                        common::Side::BUY,
                        100,
                        4,
                        6);
  oms->onOrderUpdate(&fl);
  const auto &tracked =
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY));
  EXPECT_EQ(tracked.m_qty, 6u);
  EXPECT_NE(tracked.m_state, OMOrderState::DEAD);
}

// Verifies moveOrder on a LIVE order with identical price/qty sends nothing.
TEST_F(OrderManagerTest, MoveLiveWithSamePriceSendsNothing) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::LIVE};
  oms->moveOrder(&order, 0, 100, common::Side::BUY, 10);
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies moveOrder on a LIVE order with a new price cancels first.
TEST_F(OrderManagerTest, MoveLiveWithNewPriceCancels) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::LIVE};
  oms->moveOrder(&order, 0, 101, common::Side::BUY, 10);
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  EXPECT_EQ(lastRequest().m_type, ClientRequestType::CANCEL);
}

// Verifies moveOrder from DEAD re-issues a NEW order when risk allows.
TEST_F(OrderManagerTest, MoveFromDeadReissuesNewWhenRiskAllows) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::DEAD};
  oms->moveOrder(&order, 0, 100, common::Side::BUY, 10);
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  EXPECT_EQ(lastRequest().m_type, ClientRequestType::NEW);
  EXPECT_EQ(order.m_state, OMOrderState::PENDING_NEW);
}

// Verifies moveOrder from DEAD sends nothing when risk rejects the size.
TEST_F(OrderManagerTest, MoveFromDeadBlockedByRisk) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::DEAD};
  oms->moveOrder(&order, 0, 100, common::Side::BUY, 10000);
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies moveOrders drives both bid and ask sides together.
TEST_F(OrderManagerTest, MoveOrdersDrivesBothSides) {
  oms->moveOrders(0, 99, 101, 10);
  ASSERT_NE(outReq->getNextToRead(), nullptr);
  const auto first = *outReq->getNextToRead();
  outReq->updateReadIndex();
  const auto second = *outReq->getNextToRead();
  // One BUY at 99 and one SELL at 101, both with clip qty.
  EXPECT_TRUE((first.m_side == common::Side::BUY && first.m_price == 99u &&
               second.m_side == common::Side::SELL && second.m_price == 101u) ||
              (first.m_side == common::Side::SELL && first.m_price == 101u &&
               second.m_side == common::Side::BUY && second.m_price == 99u));
  EXPECT_EQ(first.m_quantity, 10u);
  EXPECT_EQ(second.m_quantity, 10u);
}

// Verifies moveOrder on LIVE with same price but different qty sends nothing
// (book logic: price-only check, so fill-shrunk leaves ride untouched).
TEST_F(OrderManagerTest, MoveLiveWithSamePriceDifferentQtySendsNothing) {
  OMOrder order{0, 1, common::Side::BUY, 100, 10, OMOrderState::LIVE};
  oms->moveOrder(&order, 0, 100, common::Side::BUY, 20);
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies moveOrder in PENDING_NEW / PENDING_CANCEL states sends nothing.
TEST_F(OrderManagerTest, MovePendingSendsNothing) {
  OMOrder pendingNew{0,   1,  common::Side::BUY,
                     100, 10, OMOrderState::PENDING_NEW};
  oms->moveOrder(&pendingNew, 0, 101, common::Side::BUY, 10);
  OMOrder pendingCancel{0,   2,  common::Side::SELL,
                        101, 10, OMOrderState::PENDING_CANCEL};
  oms->moveOrder(&pendingCancel, 0, 102, common::Side::SELL, 10);
  EXPECT_EQ(outReq->getNextToRead(), nullptr);
}

// Verifies CANCEL_REJECTED and INVALID updates leave tracked state untouched.
TEST_F(OrderManagerTest, RejectAndInvalidUpdatesAreNoOps) {
  OMOrder order;
  oms->newOrder(&order, 0, common::Side::BUY, 100, 10);
  ClientResponseImpl acc(ClientResponseType::ACCEPTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&acc);
  ASSERT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::LIVE);
  ClientResponseImpl rej(ClientResponseType::CANCEL_REJECTED,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&rej);
  ClientResponseImpl inv(ClientResponseType::INVALID,
                         7,
                         0,
                         order.m_orderId,
                         500,
                         common::Side::BUY,
                         100,
                         0,
                         10);
  oms->onOrderUpdate(&inv);
  EXPECT_EQ(
      oms->getOMOrderSideMap(0).at(sideToIndex(common::Side::BUY)).m_state,
      OMOrderState::LIVE);
}

// Verifies consecutive newOrders allocate incrementing order ids.
TEST_F(OrderManagerTest, NewOrdersIncrementIds) {
  OMOrder o1;
  OMOrder o2;
  oms->newOrder(&o1, 0, common::Side::BUY, 100, 10);
  oms->newOrder(&o2, 0, common::Side::BUY, 101, 10);
  EXPECT_EQ(o2.m_orderId, o1.m_orderId + 1);
  ASSERT_NE(outReq->getNextToRead(), nullptr);
}
