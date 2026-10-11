#pragma once

#include "common/logging.hpp"
#include "common/time_utils.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/order_server/client_request.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/liquidity_taker.hpp"
#include "trading/strategy/market_maker.hpp"
#include "trading/strategy/market_order_book.hpp"
#include "trading/strategy/order_manager.hpp"
#include "trading/strategy/risk_manager.hpp"
#include <functional>

namespace trading {

class TradeEngine final {
public:
  TradeEngine(ClientId clientId, AlgoType algoType,
              const TradeEngineConfigMap &tickerConfig,
              exchange::ClientRequestQueue *outgoingClientRequests,
              exchange::ClientResponseQueue *incomingClientResponses,
              exchange::MarketUpdateQueue *incomingMarketUpdates);
  ~TradeEngine();

  void start();
  void stop();
  void run() noexcept;

  void
  sendClientRequest(const exchange::MEClientRequest *clientRequest) noexcept;

  void onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                         const MarketOrderBook *orderBook) noexcept;
  void onTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                     const MarketOrderBook *orderBook) noexcept;
  void onOrderUpdate(const exchange::MEClientResponse *clientResponse) noexcept;

  void defaultAlgoOnOrderBookUpdate(TickerId tickerId, Price price, Side side,
                                    const MarketOrderBook *orderBook) noexcept;
  void defaultAlgoOnTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                                const MarketOrderBook *orderBook) noexcept;
  void defaultAlgoOnOrderUpdate(
      const exchange::MEClientResponse *clientResponse) noexcept;

  std::function<void(TickerId, Price, Side, const MarketOrderBook *)>
      m_algoOnOrderBookUpdate;
  std::function<void(const exchange::MEMarketUpdate *, const MarketOrderBook *)>
      m_algoOnTradeUpdate;
  std::function<void(const exchange::MEClientResponse *)> m_algoOnOrderUpdate;

  ClientId getClientId() const noexcept {
    return m_clientId;
  }

  void initLastEventTime() noexcept {
    m_lastEventTime = common::getCurrentNanos();
  }

  auto silentSeconds() const noexcept {
    return (common::getCurrentNanos() - m_lastEventTime) / NANOS_TO_SECS;
  }

  TradeEngine(const TradeEngine &) = delete;
  TradeEngine &operator=(const TradeEngine &) = delete;
  TradeEngine(TradeEngine &&) = delete;
  TradeEngine &operator=(TradeEngine &&) = delete;

private:
  const ClientId m_clientId;

  MarketOrderBookMap m_tickerOrderBooks;

  exchange::ClientRequestQueue *m_outgoingClientRequests = nullptr;
  exchange::ClientResponseQueue *m_incomingClientResponses = nullptr;
  exchange::MarketUpdateQueue *m_incomingMarketUpdates = nullptr;

  Nanos m_lastEventTime = 0;

  std::atomic<bool> m_running = false;

  Logger m_logger;

  FeatureEngine m_featureEngine;
  PositionKeeper m_positionKeeper;
  OrderManager m_orderManager;
  RiskManager m_riskManager;

  MarketMaker *m_marketMakerAlgo = nullptr;
  LiquidityTaker *m_liquidityTakerAlgo = nullptr;
};

} // namespace trading
