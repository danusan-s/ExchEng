#include "exchange/order_server/fifo_sequencer.hpp"

namespace exchange {

void FIFOSequencer::addClientRequest(
    Nanos recvTime, const MEClientRequest &clientRequest) noexcept {
  if (m_pendingSize >= MAX_ME_PENDING_REQUESTS) [[unlikely]] {
    FATAL("FIFOSequencer pending requests exceeded max size of " +
          std::to_string(MAX_ME_PENDING_REQUESTS));
  }
  m_pendingRequests[m_pendingSize++] = {recvTime, clientRequest};
}

void FIFOSequencer::sequenceAndPublish() noexcept {
  if (m_pendingSize == 0) [[unlikely]] {
    return;
  }

  m_logger->logInfo("%:% %() Sequencing and publishing % pending requests\n",
                    __FILE__, __LINE__, __FUNCTION__, m_pendingSize);

  std::sort(m_pendingRequests.begin(),
            m_pendingRequests.begin() + m_pendingSize);

  for (size_t i = 0; i < m_pendingSize; ++i) {
    const auto &req = m_pendingRequests[i];
    m_logger->logData("%:% %() Writing request: %\n", __FILE__, __LINE__,
                      __FUNCTION__,
                      req.m_clientRequest.toString());
    auto *nextToWrite = m_clientRequestQueue->getNextToWriteTo();
    *nextToWrite = req.m_clientRequest;
    m_clientRequestQueue->updateWriteIndex();
  }

  m_pendingSize = 0;
}

} // namespace exchange
