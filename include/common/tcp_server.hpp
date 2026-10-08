#pragma once

#include "tcp_socket.hpp"

namespace common {

struct TCPServer {
  explicit TCPServer(Logger &logger)
      : m_listenerSocket(logger), m_logger(logger) {
  }

  /// Start listening for connections on the provided interface and port.
  auto listen(const std::string &iface, int port) -> void;

  /// Check for new connections or dead connections and update containers that
  /// track the sockets.
  auto poll() noexcept -> void;

  /// Publish outgoing data from the send buffer and read incoming data from the
  /// receive buffer.
  auto sendAndRecv() noexcept -> void;

private:
  /// Add and remove socket file descriptors to and from the EPOLL list.
  auto addToEpollList(TCPSocket *socket);

public:
  /// Socket on which this server is listening for new connections on.
  int m_epollFd = -1;
  TCPSocket m_listenerSocket;

  epoll_event m_events[1024];

  /// Collection of all sockets, sockets for incoming data, sockets for outgoing
  /// data and dead connections.
  std::vector<TCPSocket *> m_receiveSockets, m_sendSockets;

  /// Function wrapper to call back when data is available.
  std::function<void(TCPSocket *s, Nanos rxTime)> m_recvCallback = nullptr;
  /// Function wrapper to call back when all data across all TCPSockets has been
  /// read and dispatched this round.
  std::function<void()> m_recvFinishedCallback = nullptr;

  Logger &m_logger;
};

} // namespace common
