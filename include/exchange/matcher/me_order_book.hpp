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

  MemPool<MEOrdersAtPrice> m_ordersAtPricePool;

  MEOrdersAtPrice *m_bidsByPrice = nullptr;
  MEOrdersAtPrice *m_asksByPrice = nullptr;

  OrdersAtPriceHashMap m_priceOrdersAtPrice;

  MemPool<MEOrder> m_ordersPool;

  MEClientResponse m_clientResponse;
  MEMarketUpdate m_marketUpdate;

  OrderId m_nextMarketOrderId = 1;

  std::string m_timeStr;
  Logger *m_logger = nullptr;
};

using OrderBookHashMap = std::array<MEOrderBook *, ME_MAX_TICKERS>;

} // namespace exchange
