#include "trading/trader/order_manager.hpp"

#include "common/perf_utils.hpp"
#include "common/types.hpp"
#include "trading/trader/trade_engine.hpp"

namespace trading {

OrderManager::OrderManager(TradeEngine *tradeEngine,
                           const RiskManager &riskManager,
                           common::Logger *logger)
    : m_tradeEngine(tradeEngine), m_riskManager(riskManager), m_logger(logger) {
}

const OMOrderSideMap &OrderManager::getOMOrderSideMap(TickerId tickerId) const {
  return m_tickerSideOrders.at(tickerId);
}

void OrderManager::newOrder(OMOrder *order, TickerId tickerId, Side side,
                            Price price, Quantity qty) noexcept {
  const exchange::ClientRequestImpl newRequest(
      exchange::ClientRequestType::NEW, m_tradeEngine->getClientId(), tickerId,
      m_nextOrderId, side, price, qty);

  m_tradeEngine->sendClientRequest(&newRequest);
  *order = OMOrder{tickerId, m_nextOrderId, side,
                   price,    qty,           OMOrderState::PENDING_NEW};
  ++m_nextOrderId;

  m_logger->logData("%:% %() Sent new order % for %\n", __FILE__, __LINE__,
                    __FUNCTION__, newRequest.toString(), order->toString());
}

void OrderManager::cancelOrder(OMOrder *order) noexcept {
  const exchange::ClientRequestImpl cancelRequest(
      exchange::ClientRequestType::CANCEL, m_tradeEngine->getClientId(),
      order->m_tickerId, order->m_orderId, order->m_side, order->m_price,
      order->m_qty);

  m_tradeEngine->sendClientRequest(&cancelRequest);
  order->m_state = OMOrderState::PENDING_CANCEL;

  m_logger->logData("%:% %() Sent cancel order % for %\n", __FILE__, __LINE__,
                    __FUNCTION__, cancelRequest.toString(), order->toString());
}

void OrderManager::moveOrder(OMOrder *order, TickerId tickerId, Price price,
                             Side side, Quantity qty) noexcept {
  switch (order->m_state) {
    case OMOrderState::LIVE: {
      // Book logic: only a price change triggers cancel-replace. A qty-only
      // difference is ignored so partially filled orders ride their reduced
      // leaves instead of churning a cancel + new on every fill.
      if (order->m_price != price) {
        START_MEASURE(Trading_OrderManager_cancelOrder);
        cancelOrder(order);
        END_MEASURE(Trading_OrderManager_cancelOrder, (*m_logger));
      }
    } break;

    case OMOrderState::INVALID:
    case OMOrderState::DEAD:
      if (price != INVALID_PRICE) [[likely]] {
        START_MEASURE(Trading_OrderManager_checkPreTradeRisk);
        const auto riskResult =
            m_riskManager.checkPreTradeRisk(tickerId, side, qty);
        END_MEASURE(Trading_OrderManager_checkPreTradeRisk, (*m_logger));

        if (riskResult == RiskCheckResult::ALLOWED) [[likely]] {
          START_MEASURE(Trading_OrderManager_newOrder);
          newOrder(order, tickerId, side, price, qty);
          END_MEASURE(Trading_OrderManager_newOrder, (*m_logger));
        } else {
          m_logger->logError(
              "%:% %() Ticker: % Side: % Qty: % failed risk check: %\n",
              __FILE__, __LINE__, __FUNCTION__, tickerIdToString(tickerId),
              sideToString(side), quantityToString(qty),
              riskCheckResultToString(riskResult));
        }
      }
      break;

    case OMOrderState::PENDING_NEW:
    case OMOrderState::PENDING_CANCEL:
      break;
  }
}

void OrderManager::moveOrders(TickerId tickerId, Price bidPrice, Price askPrice,
                              Quantity clip) noexcept {
  START_MEASURE(Trading_OrderManager_moveBidOrder);
  auto &bidOrders = m_tickerSideOrders.at(tickerId).at(sideToIndex(Side::BUY));
  moveOrder(&bidOrders, tickerId, bidPrice, Side::BUY, clip);
  END_MEASURE(Trading_OrderManager_moveBidOrder, (*m_logger));

  START_MEASURE(Trading_OrderManager_moveAskOrder);
  auto &askOrders = m_tickerSideOrders.at(tickerId).at(sideToIndex(Side::SELL));
  moveOrder(&askOrders, tickerId, askPrice, Side::SELL, clip);
  END_MEASURE(Trading_OrderManager_moveAskOrder, (*m_logger));
}

void OrderManager::onOrderUpdate(
    const exchange::ClientResponseImpl *clientResponse) noexcept {
  m_logger->logData("%:% %() Received order update %\n", __FILE__, __LINE__,
                    __FUNCTION__, clientResponse->toString());
  auto &order = m_tickerSideOrders.at(clientResponse->m_tickerId)
                    .at(sideToIndex(clientResponse->m_side));

  m_logger->logData("%:% %() Orders before update: %\n", __FILE__, __LINE__,
                    __FUNCTION__, order.toString());

  switch (clientResponse->m_type) {
    case exchange::ClientResponseType::ACCEPTED:
      order.m_state = OMOrderState::LIVE;
      break;

    case exchange::ClientResponseType::CANCELED:
      order.m_state = OMOrderState::DEAD;
      break;

    case exchange::ClientResponseType::FILLED:
      order.m_qty = clientResponse->m_leavesQty;
      if (!order.m_qty) {
        order.m_state = OMOrderState::DEAD;
      }
      break;

    case exchange::ClientResponseType::CANCEL_REJECTED:
    case exchange::ClientResponseType::INVALID:
      break;
  }
}

} // namespace trading
