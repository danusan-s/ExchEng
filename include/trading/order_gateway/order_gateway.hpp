#pragma once

#include "common/logging.hpp"
#include "common/tcp_socket.hpp"
#include "common/types.hpp"
#include "exchange/order_server/client_request.hpp"
#include "exchange/order_server/client_response.hpp"

namespace trading {

class OrderGateway final {
public:
  OrderGateway(ClientId clientId,
               exchange::ClientRequestQueue *clientRequestsQueue,
               exchange::ClientResponseQueue *clientResponsesQueue,
               const std::string &ip, const std::string &iface, int port);

  ~OrderGateway();

  void start();
  void stop();
  void run() noexcept;

  void recvCallback(common::TCPSocket *socket, common::Nanos rxTime) noexcept;

  OrderGateway() = delete;
  OrderGateway(const OrderGateway &) = delete;
  OrderGateway(OrderGateway &&) = delete;
  OrderGateway &operator=(const OrderGateway &) = delete;
  OrderGateway &operator=(OrderGateway &&) = delete;

private:
  const ClientId m_clientId;

  std::string m_ip;
  const std::string m_iface;
  const int m_port = 0;

  exchange::ClientRequestQueue *m_outgoingRequests = nullptr;
  exchange::ClientResponseQueue *m_incomingResponses = nullptr;

  std::atomic<bool> m_isRunning = false;

  Logger m_logger;
  size_t m_nextOutgoingSeqNum = 1;
  size_t m_nextIncomingSeqNum = 1;
  common::TCPSocket m_socket;
};

} // namespace trading
