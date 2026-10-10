#include "trading/strategy/market_order_book.hpp"
#include "common/constants.hpp"
#include "trading/strategy/market_order.hpp"

namespace trading {

MarketOrderBook::MarketOrderBook(TickerId tickerId, Logger *logger)
    : m_tickerId(tickerId), m_ordersAtPricePool(ME_MAX_PRICE_LEVELS),
      m_orderPool(ME_MAX_ORDER_IDS), m_logger(logger) {
}

MarketOrderBook::~MarketOrderBook() {
  m_tradeEngine = nullptr;
  m_bidsByPrice = nullptr;
  m_asksByPrice = nullptr;
  m_orderHashMap.fill(nullptr);
}

void MarketOrderBook::setTradeEngine(TradeEngine *tradeEngine) {
  m_tradeEngine = tradeEngine;
}

void MarketOrderBook::onMarketUpdate(
    const exchange::MEMarketUpdate *marketUpdate) noexcept {

  // Better bid or added to best bid
  const auto bidChanged =
      (m_bidsByPrice != nullptr && marketUpdate->m_side == Side::BUY &&
       marketUpdate->m_price >= m_bidsByPrice->m_price);

  // Better ask or added to best ask
  const auto askChanged =
      (m_asksByPrice != nullptr && marketUpdate->m_side == Side::SELL &&
       marketUpdate->m_price <= m_asksByPrice->m_price);

  switch (marketUpdate->m_type) {
    case exchange::MarketUpdateType::ADD: {
      auto order = m_orderPool.allocate(
          marketUpdate->m_marketOrderId, marketUpdate->m_side,
          marketUpdate->m_price, marketUpdate->m_qty, marketUpdate->m_priority,
          nullptr, nullptr);
      addOrderToBook(order);
    } break;
    case exchange::MarketUpdateType::MODIFY: {
      auto order = m_orderHashMap.at(marketUpdate->m_marketOrderId);
      order->m_qty = marketUpdate->m_qty;
    } break;
    case exchange::MarketUpdateType::CANCEL: {
      auto order = m_orderHashMap.at(marketUpdate->m_marketOrderId);
      removeOrderFromBook(order);
    } break;
    case exchange::MarketUpdateType::TRADE: {
      m_tradeEngine->onTrade(marketUpdate, this);
      return;
    } break;
    case exchange::MarketUpdateType::CLEAR: {
      for (auto &order : m_orderHashMap) {
        if (order != nullptr) {
          m_orderPool.deallocate(order);
        }
      }
      m_orderHashMap.fill(nullptr);

      if (m_bidsByPrice != nullptr) {
        for (auto bid = m_bidsByPrice->m_nextEntry; bid != m_bidsByPrice;
             bid = bid->m_nextEntry) {
          m_ordersAtPricePool.deallocate(bid);
        }
        m_ordersAtPricePool.deallocate(m_bidsByPrice);
      }
      m_bidsByPrice = nullptr;

      if (m_asksByPrice != nullptr) {
        for (auto ask = m_asksByPrice->m_nextEntry; ask != m_asksByPrice;
             ask = ask->m_nextEntry) {
          m_ordersAtPricePool.deallocate(ask);
        }
        m_ordersAtPricePool.deallocate(m_asksByPrice);
      }
      m_asksByPrice = nullptr;
    } break;
    case exchange::MarketUpdateType::INVALID:
    case exchange::MarketUpdateType::SNAPSHOT_START:
    case exchange::MarketUpdateType::SNAPSHOT_END:
      break;
    default:
      m_logger->logError("Unknown market update type: %\n",
                         static_cast<int>(marketUpdate->m_type));
  }

  updateBBO(bidChanged, askChanged);

  m_tradeEngine->onOrderBookUpdate(marketUpdate->m_tickerId,
                                   marketUpdate->m_price, marketUpdate->m_side);
}

void MarketOrderBook::updateBBO(bool bidChanged, bool askChanged) noexcept {
  if (bidChanged) {
    if (m_bidsByPrice == nullptr) {
      m_bbo.m_bidPrice = INVALID_PRICE;
      m_bbo.m_bidQty = INVALID_QUANTITY;
    } else {
      m_bbo.m_bidPrice = m_bidsByPrice->m_price;
      m_bbo.m_bidQty = m_bidsByPrice->m_firstOrder->m_qty;
      for (auto order = m_bidsByPrice->m_firstOrder->m_nextOrder;
           order != m_bidsByPrice->m_firstOrder; order = order->m_nextOrder) {
        m_bbo.m_bidQty += order->m_qty;
      }
    }
  }

  if (askChanged) {
    if (m_asksByPrice == nullptr) {
      m_bbo.m_askPrice = INVALID_PRICE;
      m_bbo.m_askQty = INVALID_QUANTITY;
    } else {
      m_bbo.m_askPrice = m_asksByPrice->m_price;
      m_bbo.m_askQty = m_asksByPrice->m_firstOrder->m_qty;
      for (auto order = m_asksByPrice->m_firstOrder->m_nextOrder;
           order != m_asksByPrice->m_firstOrder; order = order->m_nextOrder) {
        m_bbo.m_askQty += order->m_qty;
      }
    }
  }
}

void MarketOrderBook::addOrderToBook(MarketOrder *newOrder) noexcept {
  const auto ordersAtPrice = getOrdersAtPrice(newOrder->m_price);
  if (!ordersAtPrice) {
    newOrder->m_nextOrder = newOrder;
    newOrder->m_prevOrder = newOrder;

    auto newOrdersAtPrice = m_ordersAtPricePool.allocate(
        newOrder->m_side, newOrder->m_price, newOrder, nullptr, nullptr);
    addOrdersAtPriceToBook(newOrdersAtPrice);
  } else {
    // Cyclic linked list, queue without maintaining a tail pointer
    auto firstOrder = ordersAtPrice->m_firstOrder;
    firstOrder->m_prevOrder->m_nextOrder = newOrder;
    newOrder->m_prevOrder = firstOrder->m_prevOrder;
    newOrder->m_nextOrder = firstOrder;
    firstOrder->m_prevOrder = newOrder;
  }
  m_orderHashMap.at(newOrder->m_orderId) = newOrder;
}

void MarketOrderBook::addOrdersAtPriceToBook(
    MarketOrdersAtPrice *newOrderAtPrice) noexcept {
  m_priceOrdersAtPrice.at(priceToIndex(newOrderAtPrice->m_price)) =
      newOrderAtPrice;

  const bool isBuy = newOrderAtPrice->m_side == Side::BUY;
  auto &best = isBuy ? m_bidsByPrice : m_asksByPrice;

  // Empty side: node becomes a one-element circular list
  if (!best) [[unlikely]] {
    best = newOrderAtPrice->m_nextEntry = newOrderAtPrice->m_prevEntry =
        newOrderAtPrice;
    return;
  }

  // True if node sits behind o in the book (lower bid / higher ask)
  const auto isWorseThan = [&](const MarketOrdersAtPrice *o) {
    return isBuy ? newOrderAtPrice->m_price < o->m_price
                 : newOrderAtPrice->m_price > o->m_price;
  };

  const auto insertBefore = [&](MarketOrdersAtPrice *target) {
    newOrderAtPrice->m_nextEntry = target;
    newOrderAtPrice->m_prevEntry = target->m_prevEntry;
    target->m_prevEntry->m_nextEntry = newOrderAtPrice;
    target->m_prevEntry = newOrderAtPrice;
  };

  // New best price: insert in front of the head and take over as head
  if (!isWorseThan(best)) {
    insertBefore(best);
    best = newOrderAtPrice;
    return;
  }

  // Find the first node that is not worse than the new node
  auto target = best->m_nextEntry;
  while (target != best && isWorseThan(target)) {
    target = target->m_nextEntry;
  }
  insertBefore(target);
}

void MarketOrderBook::removeOrderFromBook(MarketOrder *order) noexcept {
  auto ordersAtPrice = getOrdersAtPrice(order->m_price);
  if (order->m_prevOrder == order) {
    removeOrdersAtPriceFromBook(ordersAtPrice);
  } else {
    const auto orderBefore = order->m_prevOrder;
    const auto orderAfter = order->m_nextOrder;
    orderBefore->m_nextOrder = orderAfter;
    orderAfter->m_prevOrder = orderBefore;

    if (ordersAtPrice->m_firstOrder == order) {
      ordersAtPrice->m_firstOrder = orderAfter;
    }
  }

  order->m_nextOrder = nullptr;
  order->m_prevOrder = nullptr;
  m_orderHashMap.at(order->m_orderId) = nullptr;
  m_orderPool.deallocate(order);
}

void MarketOrderBook::removeOrdersAtPriceFromBook(
    MarketOrdersAtPrice *ordersAtPrice) noexcept {
  const bool isBuy = ordersAtPrice->m_side == Side::BUY;
  auto &best = isBuy ? m_bidsByPrice : m_asksByPrice;

  if (ordersAtPrice->m_nextEntry == ordersAtPrice) [[unlikely]] {
    best = nullptr;
  } else {
    ordersAtPrice->m_prevEntry->m_nextEntry = ordersAtPrice->m_nextEntry;
    ordersAtPrice->m_nextEntry->m_prevEntry = ordersAtPrice->m_prevEntry;

    if (best == ordersAtPrice) {
      best = ordersAtPrice->m_nextEntry;
    }
  }

  ordersAtPrice->m_nextEntry = nullptr;
  ordersAtPrice->m_prevEntry = nullptr;
  m_priceOrdersAtPrice.at(priceToIndex(ordersAtPrice->m_price)) = nullptr;
  m_ordersAtPricePool.deallocate(ordersAtPrice);
}

} // namespace trading
