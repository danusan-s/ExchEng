#include "exchange/matcher/me_order_book.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/matcher/matching_engine.hpp"

namespace exchange {

OrderId MEOrderBook::getNextMarketOrderId() noexcept {
  const auto nextId = m_nextMarketOrderId++;
  if (m_nextMarketOrderId == INVALID_ORDER_ID) [[unlikely]] {
    m_nextMarketOrderId = 1;
  }
  return nextId;
}

size_t MEOrderBook::priceToIndex(Price price) const noexcept {
  return static_cast<size_t>(price);
}

MEOrdersAtPrice *MEOrderBook::getOrdersAtPrice(Price price) noexcept {
  return m_priceOrdersAtPrice.at(priceToIndex(price));
}

MEOrderBook::MEOrderBook(TickerId ticker_id, Logger *logger,
                         MatchingEngine *matching_engine)
    : m_tickerId(ticker_id), m_matchingEngine(matching_engine),
      m_ordersAtPricePool(ME_MAX_PRICE_LEVELS), m_ordersPool(ME_MAX_ORDER_IDS),
      m_logger(logger) {
}

MEOrderBook::~MEOrderBook() {
  m_logger->logInfo("%:% %() Destroying order book for tickerId=%\n",
                    __FILE__, __LINE__, __FUNCTION__, m_tickerId);
  m_matchingEngine = nullptr;
  m_bidsByPrice = nullptr;
  m_asksByPrice = nullptr;
  for (auto &itr : m_cidOidToOrder) {
    itr.fill(nullptr);
  }
}

void MEOrderBook::add(ClientId clientId, OrderId clientOrderId,
                      TickerId tickerId, Side side, Price price,
                      Quantity qty) noexcept {
  // We add to the book but nothing executed yet
  const auto marketOrderId = getNextMarketOrderId();
  m_clientResponse = {ClientResponseType::ACCEPTED,
                      clientId,
                      tickerId,
                      clientOrderId,
                      marketOrderId,
                      side,
                      price,
                      0,
                      qty};

  m_matchingEngine->sendClientResponse(&m_clientResponse);

  const auto leavesQty = checkForMatch(clientId, clientOrderId, tickerId, side,
                                       price, qty, marketOrderId);

  if (leavesQty) [[likely]] {
    const auto priority = getNextPriority(price);
    auto newOrder = m_ordersPool.allocate(tickerId, clientId, clientOrderId,
                                          marketOrderId, side, price, leavesQty,
                                          priority, nullptr, nullptr);
    addOrderToBook(newOrder);
    m_marketUpdate = {MarketUpdateType::ADD,
                      marketOrderId,
                      tickerId,
                      side,
                      price,
                      leavesQty,
                      priority};
    m_matchingEngine->sendMarketUpdate(&m_marketUpdate);
  }
}

Priority MEOrderBook::getNextPriority(Price price) noexcept {
  auto ordersAtPrice = getOrdersAtPrice(price);
  if (ordersAtPrice == nullptr) {
    return 1lu;
  }
  // Get last added order and increment by 1
  return ordersAtPrice->m_firstOrder->m_prevOrder->m_priority + 1;
}

void MEOrderBook::addOrderToBook(MEOrder *newOrder) noexcept {
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
  m_cidOidToOrder.at(newOrder->m_clientId).at(newOrder->m_clientOrderId) =
      newOrder;
}

void MEOrderBook::addOrdersAtPriceToBook(
    MEOrdersAtPrice *newOrderAtPrice) noexcept {
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
  const auto isWorseThan = [&](const MEOrdersAtPrice *o) {
    return isBuy ? newOrderAtPrice->m_price < o->m_price
                 : newOrderAtPrice->m_price > o->m_price;
  };

  const auto insertBefore = [&](MEOrdersAtPrice *target) {
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

void MEOrderBook::cancel(ClientId clientId, OrderId orderId,
                         TickerId tickerId) noexcept {
  auto isCancellable = clientId < m_cidOidToOrder.size();
  MEOrder *order = nullptr;
  if (isCancellable) [[likely]] {
    auto &clientOrders = m_cidOidToOrder.at(clientId);
    order = clientOrders.at(orderId);
    isCancellable = order != nullptr;
  }
  if (!isCancellable) [[unlikely]] {
    m_clientResponse = {ClientResponseType::CANCEL_REJECTED,
                        clientId,
                        tickerId,
                        orderId,
                        INVALID_ORDER_ID,
                        Side::INVALID,
                        INVALID_PRICE,
                        INVALID_QUANTITY,
                        INVALID_QUANTITY};
  } else {
    m_clientResponse = {ClientResponseType::CANCELED,
                        clientId,
                        tickerId,
                        orderId,
                        order->m_marketOrderId,
                        order->m_side,
                        order->m_price,
                        INVALID_QUANTITY,
                        order->m_qty};
    m_marketUpdate = {MarketUpdateType::CANCEL,
                      order->m_marketOrderId,
                      tickerId,
                      order->m_side,
                      order->m_price,
                      0,
                      order->m_priority};
    removeOrderFromBook(order);
    m_matchingEngine->sendMarketUpdate(&m_marketUpdate);
  }
  m_matchingEngine->sendClientResponse(&m_clientResponse);
}

void MEOrderBook::removeOrderFromBook(MEOrder *order) noexcept {
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
  m_cidOidToOrder.at(order->m_clientId).at(order->m_clientOrderId) = nullptr;
  m_ordersPool.deallocate(order);
}

void MEOrderBook::removeOrdersAtPriceFromBook(
    MEOrdersAtPrice *ordersAtPrice) noexcept {
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

Quantity MEOrderBook::checkForMatch(ClientId clientId, OrderId clientOrderId,
                                    TickerId tickerId, Side side, Price price,
                                    Quantity qty,
                                    OrderId marketOrderId) noexcept {
  auto leavesQty = qty;

  if (side == Side::BUY) {
    while (leavesQty && m_asksByPrice) {
      const auto askOrders = m_asksByPrice->m_firstOrder;
      if (price < askOrders->m_price) [[likely]] {
        break;
      }

      match(tickerId, clientId, side, clientOrderId, marketOrderId, askOrders,
            &leavesQty);
    }
  } else if (side == Side::SELL) {
    while (leavesQty && m_bidsByPrice) {
      const auto bidOrders = m_bidsByPrice->m_firstOrder;
      if (price > bidOrders->m_price) [[likely]] {
        break;
      }

      match(tickerId, clientId, side, clientOrderId, marketOrderId, bidOrders,
            &leavesQty);
    }
  }
  return leavesQty;
}

void MEOrderBook::match(TickerId tickerId, ClientId clientId, Side side,
                        OrderId clientOrderId, OrderId marketOrderId,
                        MEOrder *matchOrders, Quantity *leavesQty) noexcept {
  const auto order = matchOrders;
  const auto orderQty = order->m_qty;
  const auto execQty = std::min(*leavesQty, orderQty);

  *leavesQty -= execQty;
  order->m_qty -= execQty;

  m_clientResponse = {ClientResponseType::FILLED,
                      clientId,
                      tickerId,
                      clientOrderId,
                      marketOrderId,
                      side,
                      order->m_price,
                      execQty,
                      *leavesQty};
  m_matchingEngine->sendClientResponse(&m_clientResponse);
  m_clientResponse = {ClientResponseType::FILLED,
                      order->m_clientId,
                      tickerId,
                      order->m_clientOrderId,
                      order->m_marketOrderId,
                      order->m_side,
                      order->m_price,
                      execQty,
                      order->m_qty};
  m_matchingEngine->sendClientResponse(&m_clientResponse);
  m_marketUpdate = {
      MarketUpdateType::TRADE, INVALID_ORDER_ID, tickerId,        side,
      order->m_price,          execQty,          INVALID_PRIORITY};
  m_matchingEngine->sendMarketUpdate(&m_marketUpdate);

  if (!order->m_qty) {
    m_marketUpdate = {MarketUpdateType::CANCEL,
                      order->m_marketOrderId,
                      tickerId,
                      order->m_side,
                      order->m_price,
                      orderQty,
                      INVALID_PRIORITY};
    m_matchingEngine->sendMarketUpdate(&m_marketUpdate);

    removeOrderFromBook(order);
    return;
  }

  m_marketUpdate = {MarketUpdateType::MODIFY,
                    order->m_marketOrderId,
                    tickerId,
                    order->m_side,
                    order->m_price,
                    order->m_qty,
                    order->m_priority};
  m_matchingEngine->sendMarketUpdate(&m_marketUpdate);
}

} // namespace exchange
