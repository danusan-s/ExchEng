#include "trading/order_gateway/order_gateway.hpp"
#include "common/thread_utils.hpp"
#include "exchange/order_server/client_response.hpp"
#include <string>

namespace trading {

OrderGateway::OrderGateway(ClientId clientId,
                           exchange::ClientRequestQueue *clientRequestsQueue,
                           exchange::ClientResponseQueue *clientResponsesQueue,
                           const std::string &ip, const std::string &iface,
                           int port)
    : m_clientId(clientId), m_ip(ip), m_iface(iface), m_port(port),
      m_outgoingRequests(clientRequestsQueue),
      m_incomingResponses(clientResponsesQueue),
      m_logger("trading_order_gateway_" + std::to_string(clientId) + ".log"),
      m_socket(m_logger) {
  m_socket.m_recvCallback = [this](auto socket, auto rxTime) {
    recvCallback(socket, rxTime);
  };
}

OrderGateway::~OrderGateway() {
  stop();
}

void OrderGateway::start() {
  m_isRunning.store(true, std::memory_order_relaxed);
  ASSERT(m_socket.connect(m_ip, m_iface, m_port, false) >= 0,
         "Failed to connect to" + m_ip + ":" + std::to_string(m_port) +
             " on interface " + m_iface + " error: " + std::strerror(errno));

  ASSERT(common::createAndStartThread(-1, "Trading/OrderGateway",
                                      [this]() { run(); }) != nullptr,
         "Failed to create order gateway thread");
}

void OrderGateway::stop() {
  m_isRunning.store(false, std::memory_order_relaxed);
}

void OrderGateway::run() noexcept {
  m_logger.logInfo("%:% %() Starting order gateway\n", __FILE__, __LINE__,
                   __FUNCTION__);

  while (m_isRunning.load(std::memory_order_relaxed)) {
    m_socket.sendAndRecv();
    for (auto clientReq = m_outgoingRequests->getNextToRead();
         clientReq != nullptr;
         clientReq = m_outgoingRequests->getNextToRead()) {
      m_socket.send(&m_nextOutgoingSeqNum, sizeof(m_nextOutgoingSeqNum));
      m_socket.send(clientReq, sizeof(exchange::MEClientRequest));
      m_outgoingRequests->updateReadIndex();

      m_nextOutgoingSeqNum++;
    }
  }

  m_logger.logInfo("%:% %() Stopping order gateway.\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void OrderGateway::recvCallback(common::TCPSocket *socket,
                                common::Nanos rxTime) noexcept {
  m_logger.logInfo("%:% %() Received socket:% len:% %\n", __FILE__, __LINE__,
                   __FUNCTION__, socket->m_socketFd,
                   socket->m_nextReceiveValidIndex, rxTime);

  if (socket->m_nextReceiveValidIndex >= sizeof(exchange::OMClientResponse)) {
    size_t i = 0;
    for (; i + sizeof(exchange::OMClientResponse) <=
           socket->m_nextReceiveValidIndex;
         i += sizeof(exchange::OMClientResponse)) {
      auto clientResp = reinterpret_cast<exchange::OMClientResponse *>(
          socket->m_inboundData.data() + i);
      m_logger.logInfo("%:% %() Received client response: %\n", __FILE__,
                       __LINE__, __FUNCTION__, clientResp->toString());
      if (clientResp->m_response.m_clientId != m_clientId) {
        m_logger.logError(
            "%:% %() Received client response for clientId: %. Expected: %\n",
            __FILE__, __LINE__, __FUNCTION__, clientResp->m_response.m_clientId,
            m_clientId);
        continue;
      }
      if (clientResp->m_seqNum != m_nextIncomingSeqNum) {
        m_logger.logError(
            "%:% %() Received client response with seqNum: %. Expected: %\n",
            __FILE__, __LINE__, __FUNCTION__, clientResp->m_seqNum,
            m_nextIncomingSeqNum);
        continue;
      }
      ++m_nextIncomingSeqNum;
      auto nextWrite = m_incomingResponses->getNextToWriteTo();
      *nextWrite = std::move(clientResp->m_response);
      m_incomingResponses->updateWriteIndex();
    }
    memcpy(socket->m_inboundData.data(), socket->m_inboundData.data() + i,
           socket->m_nextReceiveValidIndex - i);
    socket->m_nextReceiveValidIndex -= i;
  }
}

} // namespace trading
