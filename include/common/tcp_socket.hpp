#pragma once

#include <functional>

#include "logging.hpp"
#include "socket_utils.hpp"

namespace common {

/// Size of our send and receive buffers in bytes.
constexpr size_t TCP_BUFFER_SIZE = 64 * 1024 * 1024;

struct TCPSocket {
  explicit TCPSocket(Logger &logger) : m_logger(logger) {
    m_outboundData.resize(TCP_BUFFER_SIZE);
    m_inboundData.resize(TCP_BUFFER_SIZE);
  }

  /// Create TCPSocket with provided attributes to either listen-on /
  /// connect-to.
  auto connect(const std::string &ip, const std::string &iface, int port,
               bool isListening) -> int;

  /// Called to publish outgoing data from the buffers as well as check for and
  /// callback if data is available in the read buffers.
  auto sendAndRecv() noexcept -> bool;

  /// Write outgoing data to the send buffers.
  auto send(const void *data, size_t len) noexcept -> void;

  TCPSocket() = delete;
  TCPSocket(const TCPSocket &) = delete;
  TCPSocket(const TCPSocket &&) = delete;
  TCPSocket &operator=(const TCPSocket &) = delete;
  TCPSocket &operator=(const TCPSocket &&) = delete;

  /// File descriptor for the socket.
  int m_socketFd = -1;

  /// Send and receive buffers and trackers for read/write indices.
  std::vector<char> m_outboundData;
  size_t m_nextSendValidIndex = 0;
  std::vector<char> m_inboundData;
  size_t m_nextReceiveValidIndex = 0;

  /// Socket attributes.
  struct sockaddr_in m_socketAttrib{};

  /// Function wrapper to callback when there is data to be processed.
  std::function<void(TCPSocket *s, Nanos rxTime)> m_recvCallback = nullptr;

  Logger &m_logger;
};

} // namespace common
