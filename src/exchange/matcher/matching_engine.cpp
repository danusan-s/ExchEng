#include "exchange/matcher/matching_engine.hpp"

#include "common/perf_utils.hpp"
#include "exchange/order_server/client_request.hpp"

namespace exchange {

MatchingEngine::MatchingEngine(
    ClientRequestImplQueue *clientRequestQueue,
    ClientResponseImplQueue *clientResponseQueue,
    MarketUpdateImplQueue *marketUpdateQueue) noexcept
    : m_clientRequestQueue(clientRequestQueue),
      m_clientResponseQueue(clientResponseQueue),
      m_marketUpdateQueue(marketUpdateQueue),
      m_logger("exchange_matching_engine.log") {
  for (size_t i = 0; i < m_tickerOrderBooks.size(); ++i) {
    m_tickerOrderBooks.at(i) = std::make_unique<MEOrderBook>(
        static_cast<TickerId>(i), &m_logger, this);
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
    m_tickerOrderBooks.at(i).reset();
  }
}

void MatchingEngine::start() {
  m_running.store(true, std::memory_order_relaxed);

  auto thread =
      common::createAndStartThread(2, "MatchingEngine", [this]() { run(); });
  ASSERT(thread != nullptr, "Failed to start MatchingEngine thread.");
  thread->detach();
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
      TTT_MEASURE(T3_MatchingEngine_LFQueue_read, m_logger);

      m_logger.logData("%:% %() Processing %\n", __FILE__, __LINE__,
                       __FUNCTION__, meClientRequest->toString());

      START_MEASURE(Exchange_MatchingEngine_processClientRequest);
      processClientRequest(meClientRequest);
      END_MEASURE(Exchange_MatchingEngine_processClientRequest, m_logger);

      m_clientRequestQueue->updateReadIndex();
    }
  }

  m_logger.logInfo("%:% %() MatchingEngine Stopped\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void MatchingEngine::processClientRequest(
    const ClientRequestImpl *meClientRequest) noexcept {
  auto *orderBook = m_tickerOrderBooks.at(meClientRequest->m_tickerId).get();
  switch (meClientRequest->m_type) {
    case exchange::ClientRequestType::NEW: {
      START_MEASURE(Exchange_MEOrderBook_add);
      orderBook->add(meClientRequest->m_clientId, meClientRequest->m_orderId,
                     meClientRequest->m_tickerId, meClientRequest->m_side,
                     meClientRequest->m_price, meClientRequest->m_quantity);
      END_MEASURE(Exchange_MEOrderBook_add, m_logger);
    } break;
    case exchange::ClientRequestType::CANCEL: {
      START_MEASURE(Exchange_MEOrderBook_cancel);
      orderBook->cancel(meClientRequest->m_clientId, meClientRequest->m_orderId,
                        meClientRequest->m_tickerId);
      END_MEASURE(Exchange_MEOrderBook_cancel, m_logger);
    } break;
    default:
      m_logger.logError("%:% %() Unknown request type: %\n", __FILE__, __LINE__,
                        __FUNCTION__,
                        clientRequestTypeToString(meClientRequest->m_type));
      break;
  }
}

void MatchingEngine::sendClientResponse(
    const ClientResponseImpl *meClientResponse) noexcept {
  m_logger.logData("%:% %() Sending %\n", __FILE__, __LINE__, __FUNCTION__,
                   meClientResponse->toString());
  auto nextWriteIndex = m_clientResponseQueue->getNextToWriteTo();
  *nextWriteIndex = std::move(*meClientResponse);
  m_clientResponseQueue->updateWriteIndex();

  TTT_MEASURE(T4t_MatchingEngine_LFQueue_write, m_logger);
}

void MatchingEngine::sendMarketUpdate(
    const MarketUpdateImpl *meMarketUpdate) noexcept {
  m_logger.logData("%:% %() Sending %\n", __FILE__, __LINE__, __FUNCTION__,
                   meMarketUpdate->toString());
  auto nextWriteIndex = m_marketUpdateQueue->getNextToWriteTo();
  *nextWriteIndex = std::move(*meMarketUpdate);
  m_marketUpdateQueue->updateWriteIndex();
  TTT_MEASURE(T4_MatchingEngine_LFQueue_write, m_logger);
}

}; // namespace exchange
