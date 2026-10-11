#pragma once

#include "common/logging.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/order_manager.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace common;

namespace trading {

class LiquidityTaker final {
public:
  LiquidityTaker(TradeEngine *tradeEngine, const FeatureEngine *featureEngine,
                 OrderManager *orderManager,
                 const TradeEngineConfigMap &tickerConfig, Logger *logger);

  LiquidityTaker(const LiquidityTaker &) = delete;
  LiquidityTaker &operator=(const LiquidityTaker &) = delete;
  LiquidityTaker(LiquidityTaker &&) = delete;
  LiquidityTaker &operator=(LiquidityTaker &&) = delete;

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
