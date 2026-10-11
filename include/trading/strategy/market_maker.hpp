#pragma once

#include "common/logging.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/order_manager.hpp"
#include "trading/strategy/trade_engine.hpp"

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
  void onTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                     const MarketOrderBook *orderBook) noexcept;
  void onOrderUpdate(const exchange::MEClientResponse *clientResponse) noexcept;

private:
  const FeatureEngine *m_featureEngine = nullptr;
  OrderManager *m_orderManager = nullptr;

  Logger *m_logger = nullptr;

  const TradeEngineConfigMap m_tickerConfig;
};

} // namespace trading
