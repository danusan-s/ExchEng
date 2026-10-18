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

void FIFOSequencer::sortPendingRequests() noexcept {
  auto &requests = m_pendingRequests;

  for (size_t i = 1; i < m_pendingSize; ++i) {
    RecvTimeClientRequest elem = std::move(requests[i]);
    size_t j = i;

    while (j > 0 && elem < requests[j - 1]) {
      requests[j] = std::move(requests[j - 1]);
      --j;
    }

    requests[j] = std::move(elem);
  }
}

void FIFOSequencer::sequenceAndPublish() noexcept {
  if (m_pendingSize == 0) [[unlikely]] {
    return;
  }

  m_logger->logInfo("%:% %() Sequencing and publishing % pending requests\n",
                    __FILE__, __LINE__, __FUNCTION__, m_pendingSize);

  sortPendingRequests();

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
