#pragma once

#include "client_request.hpp"
#include "client_response.hpp"
#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/tcp_server.hpp"
#include "common/tcp_socket.hpp"
#include "fifo_sequencer.hpp"

using namespace common;

namespace exchange {

class OrderServer final {
public:
  OrderServer(ClientRequestQueue *clientRequestQueue,
              ClientResponseQueue *clientResponseQueue,
              const std::string &iface, int port) noexcept;

  ~OrderServer() noexcept;

  void start();
  void stop();
  void run() noexcept;

  OrderServer() = delete;
  OrderServer(const OrderServer &) = delete;
  OrderServer &operator=(const OrderServer &) = delete;
  OrderServer(OrderServer &&) = delete;
  OrderServer &operator=(OrderServer &&) = delete;

private:
  const std::string m_iface;
  const int m_port;

  ClientResponseQueue *m_clientResponseQueue = nullptr;

  std::string m_timeStr;
  Logger m_logger;

  TCPServer m_tcpServer;
  FIFOSequencer m_fifoSequencer;

  std::array<TCPSocket *, ME_MAX_NUM_CLIENTS> m_cidSockets;
  std::array<size_t, ME_MAX_NUM_CLIENTS> m_cidNextOutSeqNum;
  std::array<size_t, ME_MAX_NUM_CLIENTS> m_cidNextInSeqNum;

  std::atomic<bool> m_running = false;

  void recvCallback(TCPSocket *socket, Nanos rx_time) noexcept;
  void recvFinishedCallback() noexcept;
};

} // namespace exchange
