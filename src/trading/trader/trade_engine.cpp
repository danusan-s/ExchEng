#include "trading/trader/trade_engine.hpp"

#include "common/perf_utils.hpp"
#include "trading/trader/config.hpp"

namespace trading {

TradeEngine::TradeEngine(
    ClientId clientId, AlgoType algoType,
    const TradeEngineConfigMap &tickerConfig,
    exchange::ClientRequestImplQueue *outgoingClientRequests,
    exchange::ClientResponseImplQueue *incomingClientResponses,
    exchange::MarketUpdateImplQueue *incomingMarketUpdates)
    : m_clientId(clientId), m_outgoingClientRequests(outgoingClientRequests),
      m_incomingClientResponses(incomingClientResponses),
      m_incomingMarketUpdates(incomingMarketUpdates),
      m_logger("trading_engine_" + std::to_string(clientId) + ".log"),
      m_featureEngine(&m_logger), m_positionKeeper(&m_logger),
      m_orderManager(this, m_riskManager, &m_logger),
      m_riskManager(&m_positionKeeper, tickerConfig, &m_logger) {
  for (TickerId tickerId = 0; tickerId < ME_MAX_TICKERS; ++tickerId) {
    m_tickerOrderBooks.at(tickerId) =
        std::make_unique<MarketOrderBook>(tickerId, &m_logger);
    m_tickerOrderBooks.at(tickerId)->setTradeEngine(this);
  }

  m_algoOnOrderBookUpdate = [this](auto tickerId, auto price, auto side,
                                   auto orderBook) {
    this->defaultAlgoOnOrderBookUpdate(tickerId, price, side, orderBook);
  };
  m_algoOnTradeUpdate = [this](auto marketUpdate, auto orderBook) {
    this->defaultAlgoOnTradeUpdate(marketUpdate, orderBook);
  };
  m_algoOnOrderUpdate = [this](auto clientResponse) {
    this->defaultAlgoOnOrderUpdate(clientResponse);
  };

  if (algoType == AlgoType::MAKER) {
    m_marketMakerAlgo = std::make_unique<MarketMaker>(
        this, &m_featureEngine, &m_orderManager, tickerConfig, &m_logger);
  } else if (algoType == AlgoType::TAKER) {
    m_liquidityTakerAlgo = std::make_unique<LiquidityTaker>(
        this, &m_featureEngine, &m_orderManager, tickerConfig, &m_logger);
  }

  for (TickerId tickerId = 0; tickerId < ME_MAX_TICKERS; ++tickerId) {
    m_logger.logInfo("%:% %() Initialized % Ticker:% %\n", __FILE__, __LINE__,
                     __FUNCTION__, algoTypeToString(algoType),
                     tickerIdToString(tickerId),
                     tickerConfig.at(tickerId).toString());
  }
}

TradeEngine::~TradeEngine() {
  stop();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  m_marketMakerAlgo.reset();
  m_liquidityTakerAlgo.reset();

  for (TickerId tickerId = 0; tickerId < ME_MAX_TICKERS; ++tickerId) {
    m_tickerOrderBooks.at(tickerId).reset();
  }

  m_outgoingClientRequests = nullptr;
  m_incomingClientResponses = nullptr;
  m_incomingMarketUpdates = nullptr;
}

void TradeEngine::start() {
  m_running.store(true, std::memory_order_relaxed);
  auto thread = common::createAndStartThread(4, "Trading/TradeEngine",
                                             [this]() { run(); });
  ASSERT(thread != nullptr, "Failed to create trade engine thread");
  thread->detach();
}

void TradeEngine::stop() {
  while (m_incomingClientResponses->getNextToRead() != nullptr ||
         m_incomingMarketUpdates->getNextToRead() != nullptr) {
    m_logger.logInfo("%:% %() Waiting for incoming queues to drain.\n",
                     __FILE__, __LINE__, __FUNCTION__);
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(10ms);
  }

  m_logger.logData("%:% %() Positions\n%\n", __FILE__, __LINE__, __FUNCTION__,
                   m_positionKeeper.toString());

  m_running.store(false, std::memory_order_relaxed);
}

void TradeEngine::run() noexcept {
  m_logger.logInfo("%:% %() Starting trade engine\n", __FILE__, __LINE__,
                   __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    for (auto clientResp = m_incomingClientResponses->getNextToRead();
         clientResp != nullptr;
         clientResp = m_incomingClientResponses->getNextToRead()) {
      TTT_MEASURE(T9t_TradeEngine_LFQueue_read, m_logger);
      m_logger.logData("%:% %() Received client response: %\n", __FILE__,
                       __LINE__, __FUNCTION__, clientResp->toString());
      onOrderUpdate(clientResp);
      m_incomingClientResponses->updateReadIndex();
      m_lastEventTime = common::getCurrentNanos();
    }
    for (auto marketUpdate = m_incomingMarketUpdates->getNextToRead();
         marketUpdate != nullptr;
         marketUpdate = m_incomingMarketUpdates->getNextToRead()) {
      TTT_MEASURE(T9_TradeEngine_LFQueue_read, m_logger);
      m_logger.logData("%:% %() Received market update: %\n", __FILE__,
                       __LINE__, __FUNCTION__, marketUpdate->toString());
      DCHECK(marketUpdate->m_tickerId < m_tickerOrderBooks.size(),
             "Invalid tickerId: " + std::to_string(marketUpdate->m_tickerId));
      m_tickerOrderBooks.at(marketUpdate->m_tickerId)
          ->onMarketUpdate(marketUpdate);
      m_incomingMarketUpdates->updateReadIndex();
      m_lastEventTime = common::getCurrentNanos();
    }
  }

  m_logger.logInfo("%:% %() Stopping trade engine.\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void TradeEngine::sendClientRequest(
    const exchange::ClientRequestImpl *clientRequest) noexcept {
  auto nextWrite = m_outgoingClientRequests->getNextToWriteTo();
  *nextWrite = std::move(*clientRequest);
  m_outgoingClientRequests->updateWriteIndex();
  TTT_MEASURE(T10_TradeEngine_LFQueue_write, m_logger);
}

void TradeEngine::onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                                    const MarketOrderBook *orderBook) noexcept {
  const auto bbo = orderBook->getBBO();
  m_positionKeeper.updateBBO(tickerId, bbo);

  START_MEASURE(Trading_TradeEngine_onOrderBookUpdate);
  m_featureEngine.onOrderBookUpdate(tickerId, price, side, orderBook);
  END_MEASURE(Trading_TradeEngine_onOrderBookUpdate, m_logger);

  START_MEASURE(Trading_TradeEngine_algoOnOrderBookUpdate);
  m_algoOnOrderBookUpdate(tickerId, price, side, orderBook);
  END_MEASURE(Trading_TradeEngine_algoOnOrderBookUpdate, m_logger);
}

void TradeEngine::onTradeUpdate(const exchange::MarketUpdateImpl *marketUpdate,
                                const MarketOrderBook *orderBook) noexcept {
  START_MEASURE(Trading_TradeEngine_onTradeUpdate);
  m_featureEngine.onTradeUpdate(marketUpdate, orderBook);
  END_MEASURE(Trading_TradeEngine_onTradeUpdate, m_logger);

  START_MEASURE(Trading_TradeEngine_algoOnTradeUpdate);
  m_algoOnTradeUpdate(marketUpdate, orderBook);
  END_MEASURE(Trading_TradeEngine_algoOnTradeUpdate, m_logger);
}

void TradeEngine::onOrderUpdate(
    const exchange::ClientResponseImpl *clientResponse) noexcept {
  if (clientResponse->m_type == exchange::ClientResponseType::FILLED)
      [[unlikely]] {
    START_MEASURE(Trading_TradeEngine_addFill);
    m_positionKeeper.addFill(clientResponse);
    END_MEASURE(Trading_TradeEngine_addFill, m_logger);
  }

  START_MEASURE(Trading_TradeEngine_algoOnOrderUpdate);
  m_algoOnOrderUpdate(clientResponse);
  END_MEASURE(Trading_TradeEngine_algoOnOrderUpdate, m_logger);
}

void TradeEngine::defaultAlgoOnOrderBookUpdate(
    TickerId tickerId, Price price, Side side,
    const MarketOrderBook *) noexcept {
  m_logger.logInfo(
      "%:% %() Received order book update for tickerId=% price=% side=%\n",
      __FILE__, __LINE__, __FUNCTION__, tickerIdToString(tickerId),
      priceToString(price), sideToString(side));
}

void TradeEngine::defaultAlgoOnTradeUpdate(
    const exchange::MarketUpdateImpl *marketUpdate,
    const MarketOrderBook *) noexcept {
  m_logger.logData("%:% %() Received trade update %\n", __FILE__, __LINE__,
                   __FUNCTION__, marketUpdate->toString());
}

void TradeEngine::defaultAlgoOnOrderUpdate(
    const exchange::ClientResponseImpl *clientResponse) noexcept {
  m_logger.logData("%:% %() Received order update %\n", __FILE__, __LINE__,
                   __FUNCTION__, clientResponse->toString());
}

} // namespace trading
