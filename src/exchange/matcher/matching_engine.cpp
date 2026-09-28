#include "exchange/matcher/matching_engine.hpp"

namespace exchange {

MatchingEngine::MatchingEngine(ClientRequestQueue *clientRequestQueue,
                               ClientResponseQueue *clientResponseQueue,
                               MarketUpdateQueue *marketUpdateQueue) noexcept
    : m_clientRequestQueue(clientRequestQueue),
      m_clientResponseQueue(clientResponseQueue),
      m_marketUpdateQueue(marketUpdateQueue), m_logger("matching_engine.log") {
  for (size_t i = 0; i < m_tickerOrderBooks.size(); ++i) {
    m_tickerOrderBooks[i] =
        new MEOrderBook(static_cast<TickerId>(i), &m_logger, this);
  }
}

MatchingEngine::~MatchingEngine() noexcept {
  m_running.store(false, std::memory_order_relaxed);

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  m_clientRequestQueue = nullptr;
  m_clientResponseQueue = nullptr;
  m_marketUpdateQueue = nullptr;

  for (size_t i = 0; i < m_tickerOrderBooks.size(); ++i) {
    delete m_tickerOrderBooks[i];
    m_tickerOrderBooks[i] = nullptr;
  }
}

void MatchingEngine::start() {
  m_running.store(true, std::memory_order_relaxed);

  ASSERT(common::createAndStartThread(-1, "MatchingEngine",
                                      [this]() { run(); }) != nullptr,
         "Failed to start MatchingEngine thread.");
}

void MatchingEngine::stop() {
  m_running.store(false, std::memory_order_relaxed);
}

void MatchingEngine::run() noexcept {
  m_logger.log("%:% %() %\n", __FILE__, __LINE__, __FUNCTION__,
               common::getCurrentTimeStr(&m_timeStr));

  while (m_running.load(std::memory_order_relaxed)) {
    const auto meClientRequest = m_clientRequestQueue->getNextToRead();
    if (meClientRequest) [[likely]] {
      m_logger.log("%:% %() % Processing %\n", __FILE__, __LINE__, __FUNCTION__,
                   common::getCurrentTimeStr(&m_timeStr),
                   meClientRequest->toString());
      // processClientRequest(me_client_request);
      m_clientRequestQueue->updateReadIndex();
    }
  }
}

}; // namespace exchange
