#include "exchange/order_server/fifo_sequencer.hpp"
#include "common/perf_utils.hpp"

namespace exchange {

void FIFOSequencer::addClientRequest(
    Nanos recvTime, const ClientRequestImpl &clientRequest) noexcept {
  ASSERT(m_pendingSize < MAX_ME_PENDING_REQUESTS,
         "FIFOSequencer pending requests exceeded max size of " +
             std::to_string(MAX_ME_PENDING_REQUESTS));
  m_pendingRequests.at(m_pendingSize++) = {recvTime, clientRequest};
}

void FIFOSequencer::sequenceAndPublish() noexcept {
  if (m_pendingSize == 0) [[unlikely]] {
    return;
  }

  m_logger->logInfo("%:% %() Sequencing and publishing % pending requests\n",
                    __FILE__, __LINE__, __FUNCTION__, m_pendingSize);

  // Thinking was that we have a small number of pending requests (max 1024)
  // and these would be in nearly sorted order so insertion sort would be good.
  // std::sort typically uses insertion sort for small arrays in most
  // implementations
  std::sort(m_pendingRequests.begin(),
            m_pendingRequests.begin() + m_pendingSize);

  for (size_t i = 0; i < m_pendingSize; ++i) {
    const auto &req = m_pendingRequests.at(i);
    m_logger->logData("%:% %() Writing request: %\n", __FILE__, __LINE__,
                      __FUNCTION__, req.m_clientRequest.toString());
    auto *nextToWrite = m_clientRequestQueue->getNextToWriteTo();
    *nextToWrite = req.m_clientRequest;
    m_clientRequestQueue->updateWriteIndex();
    TTT_MEASURE(T2_OrderServer_LFQueue_write, (*m_logger));
  }

  m_pendingSize = 0;
}

} // namespace exchange
