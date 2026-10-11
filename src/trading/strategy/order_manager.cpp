#include "trading/strategy/order_manager.hpp"
#include "common/types.hpp"
#include "trading/strategy/trade_engine.hpp"

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
  const exchange::MEClientRequest newRequest{exchange::ClientRequestType::NEW,
                                             m_tradeEngine->getClientId(),
                                             tickerId,
                                             m_nextOrderId,
                                             side,
                                             price,
                                             qty};

  m_tradeEngine->sendClientRequest(&newRequest);
  *order = OMOrder{tickerId, m_nextOrderId, side,
                   price,    qty,           OMOrderState::PENDING_NEW};
  ++m_nextOrderId;

  m_logger->logInfo("%:% %() Sent new order % for %\n", __FILE__, __LINE__,
                    __FUNCTION__, newRequest.toString(), order->toString());
}

void OrderManager::cancelOrder(OMOrder *order) noexcept {
  const exchange::MEClientRequest cancelRequest{
      exchange::ClientRequestType::CANCEL,
      m_tradeEngine->getClientId(),
      order->m_tickerId,
      order->m_orderId,
      order->m_side,
      order->m_price,
      order->m_qty};

  m_tradeEngine->sendClientRequest(&cancelRequest);
  order->m_state = OMOrderState::PENDING_CANCEL;

  m_logger->logInfo("%:% %() Sent cancel order % for %\n", __FILE__, __LINE__,
                    __FUNCTION__, cancelRequest.toString(), order->toString());
}

void OrderManager::moveOrder(OMOrder *order, TickerId tickerId, Price price,
                             Side side, Quantity qty) noexcept {
  switch (order->m_state) {
    case OMOrderState::LIVE: {
      if (order->m_price != price || order->m_qty != qty) {
        cancelOrder(order);
      }
    } break;

    case OMOrderState::INVALID:
    case OMOrderState::DEAD:
      if (price != INVALID_PRICE) [[likely]] {
        const auto riskResult =
            m_riskManager.checkPreTradeRisk(tickerId, side, qty);
        if (riskResult == RiskCheckResult::ALLOWED) [[likely]] {
          newOrder(order, tickerId, side, price, qty);
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
  auto &bidOrders = m_tickerSideOrders.at(tickerId).at(sideToIndex(Side::BUY));
  moveOrder(&bidOrders, tickerId, bidPrice, Side::BUY, clip);

  auto &askOrders = m_tickerSideOrders.at(tickerId).at(sideToIndex(Side::SELL));
  moveOrder(&askOrders, tickerId, askPrice, Side::SELL, clip);
}

void OrderManager::onOrderUpdate(
    const exchange::MEClientResponse *clientResponse) noexcept {
  m_logger->logInfo("%:% %() Received order update %\n", __FILE__, __LINE__,
                    __FUNCTION__, clientResponse->toString());
  auto &order = m_tickerSideOrders.at(clientResponse->m_tickerId)
                    .at(sideToIndex(clientResponse->m_side));

  m_logger->logInfo("%:% %() Orders before update: %\n", __FILE__, __LINE__,
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
