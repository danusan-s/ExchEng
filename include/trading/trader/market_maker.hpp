#pragma once

#include "common/logging.hpp"
#include "trading/trader/config.hpp"
#include "trading/trader/feature_engine.hpp"
#include "trading/trader/order_manager.hpp"

using namespace common;

namespace trading {

class MarketMaker final {
public:
  MarketMaker(TradeEngine *tradeEngine, const FeatureEngine *featureEngine,
              OrderManager *orderManager,
              const TradeEngineConfigMap &tickerConfig, Logger *logger);

  MarketMaker(const MarketMaker &) = delete;
  MarketMaker &operator=(const MarketMaker &) = delete;
  MarketMaker(MarketMaker &&) = delete;
  MarketMaker &operator=(MarketMaker &&) = delete;

  void onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                         const MarketOrderBook *orderBook) noexcept;
  void onTradeUpdate(const exchange::MarketUpdateImpl *marketUpdate,
                     const MarketOrderBook *orderBook) noexcept;
  void
  onOrderUpdate(const exchange::ClientResponseImpl *clientResponse) noexcept;

private:
  const FeatureEngine *m_featureEngine = nullptr;
  OrderManager *m_orderManager = nullptr;

  Logger *m_logger = nullptr;

  const TradeEngineConfigMap m_tickerConfig;
};

} // namespace trading
