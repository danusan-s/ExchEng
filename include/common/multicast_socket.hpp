#pragma once

#include <functional>

#include "common/logging.hpp"
#include "common/socket_utils.hpp"

namespace common {

/// Size of send and receive buffers in bytes.
constexpr size_t MCAST_OUTGOING_BUFFER_SIZE = 1024 * 1024; // 1MB
constexpr size_t MCAST_INCOMING_BUFFER_SIZE =
    256; // shouldn't really receive anything

struct MulticastSocket {
  MulticastSocket(Logger &logger) : m_logger(logger) {
    m_outboundData.resize(MCAST_OUTGOING_BUFFER_SIZE);
    m_inboundData.resize(MCAST_INCOMING_BUFFER_SIZE);
  }

  /// Initialize multicast socket to read from or publish to a stream.
  /// Does not join the multicast stream yet.
  auto init(const std::string &ip, const std::string &iface, int port,
            bool is_listening) -> int;

  /// Add / Join membership / subscription to a multicast stream.
  auto join(const std::string &ip) -> bool;

  /// Remove / Leave membership / subscription to a multicast stream.
  auto leave(const std::string &ip, int port) -> void;

  /// Publish outgoing data and read incoming data.
  auto sendAndRecv() noexcept -> bool;

  /// Copy data to send buffers - does not send them out yet.
  auto send(const void *data, size_t len) noexcept -> void;

  MulticastSocket() = delete;
  MulticastSocket(const MulticastSocket &) = delete;
  MulticastSocket(MulticastSocket &&) = delete;
  MulticastSocket &operator=(const MulticastSocket &) = delete;
  MulticastSocket &operator=(MulticastSocket &&) = delete;

  int m_socketFd = -1;

  /// Send and receive buffers, typically only one or the other is needed
  std::vector<char> m_outboundData;
  size_t m_nextSendValidIndex = 0;
  std::vector<char> m_inboundData;
  size_t m_nextReceiveValidIndex = 0;

  /// Function wrapper for the method to call when data is read.
  std::function<void(MulticastSocket *s)> m_recvCallback = nullptr;

  /// Configured interface (e.g. "lo"), used to pin group membership.
  std::string m_iface;

  Logger &m_logger;
};

} // namespace common
