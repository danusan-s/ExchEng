#pragma once

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/mem_pool.hpp"
#include "common/types.hpp"

#include "exchange/market_data/market_update.hpp"
#include "trading/strategy/market_order.hpp"

using namespace common;

namespace trading {

class TradeEngine;

class MarketOrderBook final {
public:
  MarketOrderBook(TickerId tickerId, Logger *logger);

  ~MarketOrderBook();

  MarketOrderBook(const MarketOrderBook &) = delete;
  MarketOrderBook &operator=(const MarketOrderBook &) = delete;
  MarketOrderBook(MarketOrderBook &&) = delete;
  MarketOrderBook &operator=(MarketOrderBook &&) = delete;

  void setTradeEngine(TradeEngine *tradeEngine);

  void onMarketUpdate(const exchange::MEMarketUpdate *marketUpdate) noexcept;

  void addOrderToBook(MarketOrder *order) noexcept;
  void removeOrderFromBook(MarketOrder *order) noexcept;

  void addOrdersAtPriceToBook(MarketOrdersAtPrice *ordersAtPrice) noexcept;
  void removeOrdersAtPriceFromBook(MarketOrdersAtPrice *ordersAtPrice) noexcept;

  void updateBBO(bool bidChanged, bool askChanged) noexcept;

  size_t priceToIndex(Price price) const noexcept {
    return static_cast<size_t>(price / ME_MAX_PRICE_LEVELS);
  }

  MarketOrdersAtPrice *getOrdersAtPrice(Price price) const noexcept {
    return m_priceOrdersAtPrice.at(priceToIndex(price));
  }

private:
  const TickerId m_tickerId;

  TradeEngine *m_tradeEngine = nullptr;

  OrderHashMap m_orderHashMap;

  MemPool<MarketOrdersAtPrice> m_ordersAtPricePool;
  MarketOrdersAtPrice *m_asksByPrice = nullptr;
  MarketOrdersAtPrice *m_bidsByPrice = nullptr;

  OrdersAtPriceHashMap m_priceOrdersAtPrice;

  MemPool<MarketOrder> m_orderPool;

  BBO m_bbo;

  Logger *m_logger = nullptr;
};

} // namespace trading
