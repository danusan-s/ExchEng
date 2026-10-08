#pragma once

#include "common/logging.hpp"
#include "common/mem_pool.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/order_server/client_response.hpp"

#include "me_order.hpp"

using namespace common;

namespace exchange {

class MatchingEngine;

class MEOrderBook final {
public:
  explicit MEOrderBook(TickerId tickerId, Logger *logger,
                       MatchingEngine *matchingEngine);

  ~MEOrderBook();

  void add(ClientId clientId, OrderId clientOrderId, TickerId tickerId,
           Side side, Price price, Quantity qty) noexcept;

  void cancel(ClientId clientId, OrderId orderId, TickerId tickerId) noexcept;

  std::string toString(bool detailed, bool validityCheck) const;

  MEOrderBook() = delete;
  MEOrderBook(const MEOrderBook &) = delete;
  MEOrderBook(const MEOrderBook &&) = delete;
  MEOrderBook &operator=(const MEOrderBook &) = delete;
  MEOrderBook &operator=(const MEOrderBook &&) = delete;

private:
  TickerId m_tickerId = INVALID_TICKER_ID;
  MatchingEngine *m_matchingEngine = nullptr;

  ClientOrderHashMap m_cidOidToOrder;

  MemPool<MEOrdersAtPrice> m_ordersAtPricePool;

  MEOrdersAtPrice *m_bidsByPrice = nullptr;
  MEOrdersAtPrice *m_asksByPrice = nullptr;

  OrdersAtPriceHashMap m_priceOrdersAtPrice;

  MemPool<MEOrder> m_ordersPool;

  // Temporary objects to avoid allocation for each response and market update
  MEClientResponse m_clientResponse;
  MEMarketUpdate m_marketUpdate;

  OrderId m_nextMarketOrderId = 1;

  Logger *m_logger = nullptr;

  OrderId getNextMarketOrderId() noexcept;
  size_t priceToIndex(Price price) const noexcept;
  MEOrdersAtPrice *getOrdersAtPrice(Price price) noexcept;
  Priority getNextPriority(Price price) noexcept;
  void addOrderToBook(MEOrder *newOrder) noexcept;
  void addOrdersAtPriceToBook(MEOrdersAtPrice *ordersAtPrice) noexcept;
  void removeOrderFromBook(MEOrder *order) noexcept;
  void removeOrdersAtPriceFromBook(MEOrdersAtPrice *ordersAtPrice) noexcept;
  Quantity checkForMatch(ClientId clientId, OrderId clientOrderId,
                         TickerId tickerId, Side side, Price price,
                         Quantity qty, OrderId marketOrderId) noexcept;
  void match(TickerId tickerId, ClientId clientId, Side side,
             OrderId clientOrderId, OrderId marketOrderId, MEOrder *matchOrders,
             Quantity *matchQty) noexcept;
};

using OrderBookHashMap = std::array<MEOrderBook *, ME_MAX_TICKERS>;

} // namespace exchange
