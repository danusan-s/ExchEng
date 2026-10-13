#include "exchange/order_server/order_server.hpp"
#include "common/macros.hpp"
#include "common/perf_utils.hpp"
#include "exchange/order_server/client_request.hpp"

namespace exchange {

OrderServer::OrderServer(ClientRequestImplQueue *clientRequestQueue,
                         ClientResponseImplQueue *clientResponseQueue,
                         const std::string &iface, int port) noexcept
    : m_iface(iface), m_port(port), m_clientResponseQueue(clientResponseQueue),
      m_logger("exchange_order_server.log"), m_tcpServer(m_logger),
      m_fifoSequencer(clientRequestQueue, &m_logger) {
  m_cidNextOutSeqNum.fill(1);
  m_cidNextInSeqNum.fill(1);
  m_cidSockets.fill(nullptr);

  m_tcpServer.m_recvCallback = [this](auto socket, auto rx_time) {
    recvCallback(socket, rx_time);
  };

  m_tcpServer.m_recvFinishedCallback = [this]() { recvFinishedCallback(); };
}

OrderServer::~OrderServer() noexcept {
  stop();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);
}

void OrderServer::start() {
  m_running.store(true, std::memory_order_relaxed);
  m_tcpServer.listen(m_iface, m_port);

  auto thread =
      common::createAndStartThread(3, "OrderServer", [this]() { run(); });
  ASSERT(thread != nullptr, "Failed to start OrderServer thread.");
  thread->detach();
}

void OrderServer::stop() {
  m_running.store(false, std::memory_order_relaxed);
}

void OrderServer::run() noexcept {
  m_logger.logInfo("%:% %() OrderServer started on iface:% port:%\n", __FILE__,
                   __LINE__, __FUNCTION__, m_iface, m_port);

  while (m_running.load(std::memory_order_relaxed)) {
    m_tcpServer.poll();
    m_tcpServer.sendAndRecv();

    for (auto clientResponse = m_clientResponseQueue->getNextToRead();
         clientResponse != nullptr;
         clientResponse = m_clientResponseQueue->getNextToRead()) {
      auto &nextOutSeqNum = m_cidNextOutSeqNum.at(clientResponse->m_clientId);
      TTT_MEASURE(T5t_OrderServer_LFQueue_read, m_logger);

      m_logger.logData("%:% %() Sending response to ClientId: % seqNum: % %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       clientResponse->m_clientId, nextOutSeqNum,
                       clientResponse->toString().c_str());

      auto socket = m_cidSockets.at(clientResponse->m_clientId);
      ASSERT(socket != nullptr, "No socket found for ClientId: " +
                                    std::to_string(clientResponse->m_clientId));

      START_MEASURE(Exchange_OrderServer_send);
      socket->send(&nextOutSeqNum, sizeof(nextOutSeqNum));
      socket->send(clientResponse, sizeof(ClientResponseImpl));
      END_MEASURE(Exchange_OrderServer_send, m_logger);

      ++nextOutSeqNum;
      m_clientResponseQueue->updateReadIndex();

      TTT_MEASURE(T6t_OrderServer_TCP_write, m_logger);
    }
  }

  m_logger.logInfo("%:% %() OrderServer stopped\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void OrderServer::recvCallback(TCPSocket *socket, Nanos rxTime) noexcept {
  m_logger.logInfo("%:% %() Received data from socket % at time %\n", __FILE__,
                   __LINE__, __FUNCTION__, socket->m_socketFd, rxTime);

  TTT_MEASURE(T1_OrderServer_TCP_read, m_logger);

  if (socket->m_nextReceiveValidIndex > sizeof(ClientRequest)) {
    size_t i = 0;
    for (; i + sizeof(ClientRequest) <= socket->m_nextReceiveValidIndex;
         i += sizeof(ClientRequest)) {
      const auto *request = reinterpret_cast<const ClientRequest *>(
          socket->m_inboundData.data() + i);

      m_logger.logData("%:% %() Processing request from socket %: %\n",
                       __FILE__, __LINE__, __FUNCTION__, socket->m_socketFd,
                       request->toString());

      // First request from client
      if (m_cidSockets.at(request->m_request.m_clientId) == nullptr)
          [[unlikely]] {
        m_cidSockets.at(request->m_request.m_clientId) = socket;
      }
      // Mismatch in client socket
      if (m_cidSockets.at(request->m_request.m_clientId) != socket) {
        m_logger.logError(
            "%:% %() Received request from ClientId: % on different "
            "socket: % expected: %\n",
            __FILE__, __LINE__, __FUNCTION__, request->m_request.m_clientId,
            socket->m_socketFd,
            m_cidSockets.at(request->m_request.m_clientId)->m_socketFd);
        continue;
      }

      // TODO: REJECT_BACK for mismatched seqNum, so client can retry
      // Check for sequence number mismatch
      auto &nextInSeqNum = m_cidNextInSeqNum.at(request->m_request.m_clientId);
      if (request->m_seqNum != nextInSeqNum) {
        m_logger.logError("%:% %() Received request from ClientId: % with "
                          "seqNum: % expected seqNum: %\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          request->m_request.m_clientId, request->m_seqNum,
                          nextInSeqNum);
        continue;
      }
      ++nextInSeqNum;

      START_MEASURE(Exchange_OrderServer_FIFO_addClientRequest);
      m_fifoSequencer.addClientRequest(rxTime, request->m_request);
      END_MEASURE(Exchange_OrderServer_FIFO_addClientRequest, m_logger);
    }
    memcpy(socket->m_inboundData.data(), socket->m_inboundData.data() + i,
           socket->m_nextReceiveValidIndex - i);
    socket->m_nextReceiveValidIndex -= i;
  }
}

void OrderServer::recvFinishedCallback() noexcept {
  START_MEASURE(Exchange_OrderServer_FIFO_sequenceAndPublish);
  m_logger.logInfo("%:% %() Finished processing received data\n", __FILE__,
                   __LINE__, __FUNCTION__);
  m_fifoSequencer.sequenceAndPublish();
  END_MEASURE(Exchange_OrderServer_FIFO_sequenceAndPublish, m_logger);
}

} // namespace exchange
