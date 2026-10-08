#include "exchange/matcher/matching_engine.hpp"
#include "exchange/order_server/client_request.hpp"

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
  m_logger.logInfo("%:% %() MatchingEngine Started\n", __FILE__, __LINE__,
                   __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    const auto meClientRequest = m_clientRequestQueue->getNextToRead();
    if (meClientRequest) [[likely]] {
      m_logger.logData("%:% %() Processing %\n", __FILE__, __LINE__,
                       __FUNCTION__,
                       meClientRequest->toString());
      processClientRequest(meClientRequest);
      m_clientRequestQueue->updateReadIndex();
    }
  }

  m_logger.logInfo("%:% %() MatchingEngine Stopped\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void MatchingEngine::processClientRequest(
    const MEClientRequest *meClientRequest) noexcept {
  auto orderBook = m_tickerOrderBooks[meClientRequest->m_tickerId];
  switch (meClientRequest->m_type) {
    case exchange::ClientRequestType::NEW: {
      orderBook->add(meClientRequest->m_clientId, meClientRequest->m_orderId,
                     meClientRequest->m_tickerId, meClientRequest->m_side,
                     meClientRequest->m_price, meClientRequest->m_quantity);

    } break;
    case exchange::ClientRequestType::CANCEL: {
      orderBook->cancel(meClientRequest->m_clientId, meClientRequest->m_orderId,
                        meClientRequest->m_tickerId);
    } break;
    default:
      m_logger.logError("%:% %() Unknown request type: %\n", __FILE__,
                       __LINE__, __FUNCTION__,
                       clientRequestTypeToString(meClientRequest->m_type));
      break;
  }
}

void MatchingEngine::sendClientResponse(
    const MEClientResponse *meClientResponse) noexcept {
  m_logger.logData("%:% %() Sending %\n", __FILE__, __LINE__, __FUNCTION__,
                   meClientResponse->toString());
  auto nextWriteIndex = m_clientResponseQueue->getNextToWriteTo();
  *nextWriteIndex = std::move(*meClientResponse);
  m_clientResponseQueue->updateWriteIndex();
}

void MatchingEngine::sendMarketUpdate(
    const MEMarketUpdate *meMarketUpdate) noexcept {
  m_logger.logData("%:% %() Sending %\n", __FILE__, __LINE__, __FUNCTION__,
                   meMarketUpdate->toString());
  auto nextWriteIndex = m_marketUpdateQueue->getNextToWriteTo();
  *nextWriteIndex = std::move(*meMarketUpdate);
  m_marketUpdateQueue->updateWriteIndex();
}

}; // namespace exchange
