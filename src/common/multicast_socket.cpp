#include "common/multicast_socket.hpp"

namespace common {

/// Initialize multicast socket to read from or publish to a stream.
/// Does not join the multicast stream yet.
auto MulticastSocket::init(const std::string &ip, const std::string &iface,
                           int port, bool is_listening) -> int {
  const SocketCfg socket_cfg{ip, iface, port, true, is_listening, false};
  m_socketFd = createSocket(m_logger, socket_cfg);
  return m_socketFd;
}

/// Add / Join membership / subscription to a multicast stream.
auto MulticastSocket::join(const std::string &ip) -> bool {
  return common::join(m_socketFd, ip);
}

/// Remove / Leave membership / subscription to a multicast stream.
auto MulticastSocket::leave(const std::string &, int) -> void {
  close(m_socketFd);
  m_socketFd = -1;
}

/// Publish outgoing data and read incoming data.
auto MulticastSocket::sendAndRecv() noexcept -> bool {
  // Read data and dispatch callbacks if data is available - non blocking.
  const ssize_t n_rcv =
      recv(m_socketFd, m_inboundData.data() + m_nextReceiveValidIndex,
           MCAST_BUFFER_SIZE - m_nextReceiveValidIndex, MSG_DONTWAIT);
  if (n_rcv > 0) {
    m_nextReceiveValidIndex += n_rcv;
    m_logger.logData("%:% %() % read socket:% len:%\n", __FILE__, __LINE__,
                     __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                     m_socketFd, m_nextReceiveValidIndex);
    m_recvCallback(this);
  }

  // Publish market data in the send buffer to the multicast stream.
  if (m_nextSendValidIndex > 0) {
    ssize_t n = ::send(m_socketFd, m_outboundData.data(), m_nextSendValidIndex,
                       MSG_DONTWAIT | MSG_NOSIGNAL);

    m_logger.logData("%:% %() % send socket:% len:%\n", __FILE__, __LINE__,
                     __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                     m_socketFd, n);
  }
  m_nextSendValidIndex = 0;

  return (n_rcv > 0);
}

/// Copy data to send buffers - does not send them out yet.
auto MulticastSocket::send(const void *data, size_t len) noexcept -> void {
  memcpy(m_outboundData.data() + m_nextSendValidIndex, data, len);
  m_nextSendValidIndex += len;
  ASSERT(m_nextSendValidIndex < MCAST_BUFFER_SIZE,
         "Mcast socket buffer filled up and sendAndRecv() not called.");
}

} // namespace common
