#include "common/tcp_server.hpp"

namespace common {

/// Add and remove socket file descriptors to and from the EPOLL list.
auto TCPServer::addToEpollList(TCPSocket *socket) {
  epoll_event ev{EPOLLET | EPOLLIN, {reinterpret_cast<void *>(socket)}};
  return !epoll_ctl(m_epollFd, EPOLL_CTL_ADD, socket->m_socketFd, &ev);
}

/// Start listening for connections on the provided interface and port.
auto TCPServer::listen(const std::string &iface, int port) -> void {
  m_epollFd = epoll_create(1);
  ASSERT(m_epollFd >= 0,
         "epoll_create() failed error:" + std::string(std::strerror(errno)));

  ASSERT(m_listenerSocket.connect("", iface, port, true) >= 0,
         "Listener socket failed to connect. iface:" + iface +
             " port:" + std::to_string(port) +
             " error:" + std::string(std::strerror(errno)));

  ASSERT(addToEpollList(&m_listenerSocket),
         "epoll_ctl() failed. error:" + std::string(std::strerror(errno)));
}

/// Publish outgoing data from the send buffer and read incoming data from the
/// receive buffer.
auto TCPServer::sendAndRecv() noexcept -> void {
  auto recv = false;

  std::for_each(m_receiveSockets.begin(), m_receiveSockets.end(),
                [&recv](auto socket) { recv |= socket->sendAndRecv(); });

  if (recv) // There were some events and they have all been dispatched, inform
            // listener.
    m_recvFinishedCallback();

  std::for_each(m_sendSockets.begin(), m_sendSockets.end(),
                [](auto socket) { socket->sendAndRecv(); });
}

/// Check for new connections or dead connections and update containers that
/// track the sockets.
auto TCPServer::poll() noexcept -> void {
  const int maxEvents = 1 + m_sendSockets.size() + m_receiveSockets.size();

  const int n = epoll_wait(m_epollFd, m_events, maxEvents, 0);
  bool haveNewConnection = false;
  for (int i = 0; i < n; ++i) {
    const auto &event = m_events[i];
    auto socket = reinterpret_cast<TCPSocket *>(event.data.ptr);

    // Check for new connections.
    if (event.events & EPOLLIN) {
      if (socket == &m_listenerSocket) {
        m_logger.log("%:% %() % EPOLLIN listener_socket:%\n", __FILE__,
                     __LINE__, __FUNCTION__,
                     common::getCurrentTimeStr(&m_timeStr), socket->m_socketFd);
        haveNewConnection = true;
        continue;
      }
      m_logger.log("%:% %() % EPOLLIN socket:%\n", __FILE__, __LINE__,
                   __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                   socket->m_socketFd);
      if (std::find(m_receiveSockets.begin(), m_receiveSockets.end(), socket) ==
          m_receiveSockets.end())
        m_receiveSockets.push_back(socket);
    }

    if (event.events & EPOLLOUT) {
      m_logger.log("%:% %() % EPOLLOUT socket:%\n", __FILE__, __LINE__,
                   __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                   socket->m_socketFd);
      if (std::find(m_sendSockets.begin(), m_sendSockets.end(), socket) ==
          m_sendSockets.end())
        m_sendSockets.push_back(socket);
    }

    if (event.events & (EPOLLERR | EPOLLHUP)) {
      m_logger.log("%:% %() % EPOLLERR socket:%\n", __FILE__, __LINE__,
                   __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                   socket->m_socketFd);
      if (std::find(m_receiveSockets.begin(), m_receiveSockets.end(), socket) ==
          m_receiveSockets.end())
        m_receiveSockets.push_back(socket);
    }
  }

  // Accept a new connection, create a TCPSocket and add it to our containers.
  while (haveNewConnection) {
    m_logger.log("%:% %() % haveNewConnection\n", __FILE__, __LINE__,
                 __FUNCTION__, common::getCurrentTimeStr(&m_timeStr));
    sockaddr_storage addr;
    socklen_t addrLen = sizeof(addr);
    int fd = accept(m_listenerSocket.m_socketFd,
                    reinterpret_cast<sockaddr *>(&addr), &addrLen);
    if (fd == -1)
      break;

    ASSERT(setNonBlocking(fd) && disableNagle(fd),
           "Failed to set non-blocking or no-delay on socket:" +
               std::to_string(fd));

    m_logger.log("%:% %() % accepted socket:%\n", __FILE__, __LINE__,
                 __FUNCTION__, common::getCurrentTimeStr(&m_timeStr), fd);

    auto socket = new TCPSocket(m_logger);
    socket->m_socketFd = fd;
    socket->m_recvCallback = m_recvCallback;
    ASSERT(addToEpollList(socket),
           "Unable to add socket. error:" + std::string(std::strerror(errno)));

    if (std::find(m_receiveSockets.begin(), m_receiveSockets.end(), socket) ==
        m_receiveSockets.end())
      m_receiveSockets.push_back(socket);
  }
}

} // namespace common
