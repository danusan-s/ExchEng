#pragma once

#include "common/logging.hpp"
#include "exchange/order_server/client_response.hpp"
#include "trading/trader/om_order.hpp"
#include "trading/trader/risk_manager.hpp"

using namespace common;

namespace trading {

class TradeEngine;

class OrderManager final {
public:
  OrderManager(TradeEngine *tradeEngine, const RiskManager &riskManager,
               common::Logger *logger);

  OrderManager() = delete;
  OrderManager(const OrderManager &) = delete;
  OrderManager(OrderManager &&) = delete;
  OrderManager &operator=(const OrderManager &) = delete;
  OrderManager &operator=(OrderManager &&) = delete;

  const OMOrderSideMap &getOMOrderSideMap(TickerId tickerId) const;

  void newOrder(OMOrder *order, TickerId tickerId, Side side, Price price,
                Quantity qty) noexcept;
  void cancelOrder(OMOrder *order) noexcept;
  void moveOrder(OMOrder *order, TickerId tickerId, Price price, Side side,
                 Quantity qty) noexcept;
  void moveOrders(TickerId tickerId, Price bidPrice, Price askPrice,
                  Quantity clip) noexcept;
  void
  onOrderUpdate(const exchange::ClientResponseImpl *clientResponse) noexcept;

private:
  TradeEngine *m_tradeEngine = nullptr;
  const RiskManager &m_riskManager;

  common::Logger *m_logger = nullptr;

  OMOrderTickerSideMap m_tickerSideOrders;
  OrderId m_nextOrderId = 1;
};

} // namespace trading
