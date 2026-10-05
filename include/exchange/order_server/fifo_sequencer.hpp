#pragma once

#include "common/logging.hpp"
#include "exchange/order_server/client_request.hpp"

using namespace common;

namespace exchange {

constexpr size_t MAX_ME_PENDING_REQUESTS = 1024;

class FIFOSequencer final {
public:
  FIFOSequencer(ClientRequestQueue *clientRequestQueue, Logger *logger)
      : m_clientRequestQueue(clientRequestQueue), m_logger(logger) {
  }
  ~FIFOSequencer() = default;

  FIFOSequencer() = delete;
  FIFOSequencer(const FIFOSequencer &) = delete;
  FIFOSequencer &operator=(const FIFOSequencer &) = delete;
  FIFOSequencer(FIFOSequencer &&) = delete;
  FIFOSequencer &operator=(FIFOSequencer &&) = delete;

  void addClientRequest(Nanos recvTime,
                        const MEClientRequest &clientRequest) noexcept;
  void sequenceAndPublish() noexcept;

private:
  ClientRequestQueue *m_clientRequestQueue = nullptr;
  std::string m_timeStr;
  Logger *m_logger = nullptr;

  struct RecvTimeClientRequest {
    Nanos m_recvTime = 0;
    MEClientRequest m_clientRequest;

    auto operator<(const RecvTimeClientRequest &other) const noexcept {
      return m_recvTime < other.m_recvTime;
    }
  };

  std::array<RecvTimeClientRequest, MAX_ME_PENDING_REQUESTS> m_pendingRequests;
  size_t m_pendingSize = 0;
};

} // namespace exchange
