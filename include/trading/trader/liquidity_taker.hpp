#pragma once

#include "common/logging.hpp"
#include "trading/trader/config.hpp"
#include "trading/trader/feature_engine.hpp"
#include "trading/trader/order_manager.hpp"

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
