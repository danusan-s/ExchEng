#pragma once

#include <functional>
#include <memory>

#include "common/logging.hpp"
#include "common/time_utils.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/order_server/client_request.hpp"
#include "trading/trader/config.hpp"
#include "trading/trader/feature_engine.hpp"
#include "trading/trader/liquidity_taker.hpp"
#include "trading/trader/market_maker.hpp"
#include "trading/trader/market_order_book.hpp"
#include "trading/trader/order_manager.hpp"
#include "trading/trader/risk_manager.hpp"

namespace trading {

class TradeEngine final {
public:
  TradeEngine(ClientId clientId, AlgoType algoType,
              const TradeEngineConfigMap &tickerConfig,
              exchange::ClientRequestImplQueue *outgoingClientRequests,
              exchange::ClientResponseImplQueue *incomingClientResponses,
              exchange::MarketUpdateImplQueue *incomingMarketUpdates);
  ~TradeEngine();

  void start();
  void stop();
  void run() noexcept;

  void
  sendClientRequest(const exchange::ClientRequestImpl *clientRequest) noexcept;

  void onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                         const MarketOrderBook *orderBook) noexcept;
  void onTradeUpdate(const exchange::MarketUpdateImpl *marketUpdate,
                     const MarketOrderBook *orderBook) noexcept;
  void
  onOrderUpdate(const exchange::ClientResponseImpl *clientResponse) noexcept;

  void defaultAlgoOnOrderBookUpdate(TickerId tickerId, Price price, Side side,
                                    const MarketOrderBook *orderBook) noexcept;
  void defaultAlgoOnTradeUpdate(const exchange::MarketUpdateImpl *marketUpdate,
                                const MarketOrderBook *orderBook) noexcept;
  void defaultAlgoOnOrderUpdate(
      const exchange::ClientResponseImpl *clientResponse) noexcept;

  std::function<void(TickerId, Price, Side, const MarketOrderBook *)>
      m_algoOnOrderBookUpdate;
  std::function<void(const exchange::MarketUpdateImpl *,
                     const MarketOrderBook *)>
      m_algoOnTradeUpdate;
  std::function<void(const exchange::ClientResponseImpl *)> m_algoOnOrderUpdate;

  ClientId getClientId() const noexcept {
    return m_clientId;
  }

  // Testability: expose position state for verification.
  const PositionInfo &getPositionInfo(TickerId tickerId) const noexcept {
    return m_positionKeeper.getPositionInfo(tickerId);
  }

  // Testability: expose a ticker book so tests can drive market data through
  // the engine's real run-path objects without extra ownership hazards.
  MarketOrderBook *getOrderBook(TickerId tickerId) const noexcept {
    return m_tickerOrderBooks.at(tickerId).get();
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

  exchange::ClientRequestImplQueue *m_outgoingClientRequests = nullptr;
  exchange::ClientResponseImplQueue *m_incomingClientResponses = nullptr;
  exchange::MarketUpdateImplQueue *m_incomingMarketUpdates = nullptr;

  Nanos m_lastEventTime = 0;

  std::atomic<bool> m_running = false;

  Logger m_logger;

  FeatureEngine m_featureEngine;
  PositionKeeper m_positionKeeper;
  OrderManager m_orderManager;
  RiskManager m_riskManager;

  std::unique_ptr<MarketMaker> m_marketMakerAlgo = nullptr;
  std::unique_ptr<LiquidityTaker> m_liquidityTakerAlgo = nullptr;
};

} // namespace trading
