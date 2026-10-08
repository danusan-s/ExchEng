#pragma once

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <ifaddrs.h>
#include <iostream>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sstream>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "macros.hpp"

#include "logging.hpp"

namespace common {
struct SocketCfg {
  std::string m_ip;
  std::string m_iface;
  int m_port = -1;
  bool m_isUDP = false;
  bool m_isListening = false;
  bool m_needsSOTimestamp = false;

  auto toString() const {
    std::stringstream ss;
    ss << "SocketCfg[ip:" << m_ip << " iface:" << m_iface << " port:" << m_port
       << " is_udp:" << m_isUDP << " is_listening:" << m_isListening
       << " needs_SO_timestamp:" << m_needsSOTimestamp << "]";

    return ss.str();
  }
};

constexpr int MAX_TCP_SERVER_BACKLOG = 1024;

// Convert interface name "eth0" to ip "123.123.123.123".
inline std::string getIfaceIP(const std::string &iface) {
  char buf[NI_MAXHOST] = {'\0'};
  ifaddrs *ifaddr = nullptr;

  if (getifaddrs(&ifaddr) != -1) {
    for (ifaddrs *ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET &&
          iface == ifa->ifa_name) {
        getnameinfo(ifa->ifa_addr, sizeof(sockaddr_in), buf, sizeof(buf), NULL,
                    0, NI_NUMERICHOST);
        break;
      }
    }
    freeifaddrs(ifaddr);
  }

  return buf;
}

// Sockets will not block on read
inline bool setNonBlocking(int fd) {
  const auto flags = fcntl(fd, F_GETFL, 0);
  if (flags & O_NONBLOCK)
    return true;
  return (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != -1);
}

// Disable Nagle's algorithm and associated delays.
// Nagle's algo waits for packets to accumulate before firing them off
// Avoids overhead from firing small packets but adds latency
inline bool disableNagle(int fd) {
  int one = 1;
  return (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY,
                     reinterpret_cast<void *>(&one), sizeof(one)) != -1);
}

/// Allow software receive timestamps on incoming packets.
inline bool setSOTimestamp(int fd) {
  int one = 1;
  return (setsockopt(fd, SOL_SOCKET, SO_TIMESTAMP,
                     reinterpret_cast<void *>(&one), sizeof(one)) != -1);
}

/// Add / Join membership / subscription to the multicast stream specified and
/// on the interface specified.
inline bool join(int fd, const std::string &ip) {
  const ip_mreq mreq{{inet_addr(ip.c_str())}, {htonl(INADDR_ANY)}};
  return (setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) !=
          -1);
}

/// Create a TCP / UDP socket to either connect to or listen for data on or
/// listen for connections on the specified interface and IP:port information.
[[nodiscard]] inline int createSocket(Logger &logger,
                                      const SocketCfg &socketCfg) {
  std::string timeStr;

  const auto ip =
      socketCfg.m_ip.empty() ? getIfaceIP(socketCfg.m_iface) : socketCfg.m_ip;
  logger.logData("%:% %() % cfg:%\n", __FILE__, __LINE__, __FUNCTION__,
                 common::getCurrentTimeStr(&timeStr), socketCfg.toString());

  const int inputFlags = (socketCfg.m_isListening ? AI_PASSIVE : 0) |
                         (AI_NUMERICHOST | AI_NUMERICSERV);
  const addrinfo hints{inputFlags,
                       AF_INET,
                       socketCfg.m_isUDP ? SOCK_DGRAM : SOCK_STREAM,
                       socketCfg.m_isUDP ? IPPROTO_UDP : IPPROTO_TCP,
                       0,
                       0,
                       nullptr,
                       nullptr};

  addrinfo *result = nullptr;
  const auto rc = getaddrinfo(
      ip.c_str(), std::to_string(socketCfg.m_port).c_str(), &hints, &result);
  ASSERT(!rc, "getaddrinfo() failed. error:" + std::string(gai_strerror(rc)) +
                  "errno:" + strerror(errno));

  int socketFd = -1;
  int one = 1;
  for (addrinfo *rp = result; rp; rp = rp->ai_next) {
    // Create the socket
    ASSERT((socketFd =
                socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol)) != -1,
           "socket() failed. errno:" + std::string(strerror(errno)));

    // Set non blocking
    ASSERT(setNonBlocking(socketFd),
           "setNonBlocking() failed. errno:" + std::string(strerror(errno)));

    // Disable nagle for tcp
    if (!socketCfg.m_isUDP) {
      ASSERT(disableNagle(socketFd),
             "disableNagle() failed. errno:" + std::string(strerror(errno)));
    }

    // If not listening socket, then connect to dest addr
    if (!socketCfg.m_isListening) {
      ASSERT(connect(socketFd, rp->ai_addr, rp->ai_addrlen) != -1,
             "connect() failed. errno:" + std::string(strerror(errno)));
    }

    // If listening socket, allow reuse addr since ports are locked in linux
    // after let's say a crash for a few minutes, this avoids it.
    if (socketCfg.m_isListening) {
      ASSERT(setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR,
                        reinterpret_cast<const char *>(&one), sizeof(one)) == 0,
             "setsockopt() SO_REUSEADDR failed. errno:" +
                 std::string(strerror(errno)));
    }

    // If listening socket, bind to port to receive connections
    if (socketCfg.m_isListening) {
      const sockaddr_in addr{
          AF_INET, htons(socketCfg.m_port), {htonl(INADDR_ANY)}, {}};
      ASSERT(bind(socketFd,
                  socketCfg.m_isUDP
                      ? reinterpret_cast<const struct sockaddr *>(&addr)
                      : rp->ai_addr,
                  sizeof(addr)) == 0,
             "bind() failed. errno:%" + std::string(strerror(errno)));
    }

    // Handle TCP connections (each client gets own new fd)
    // UDP doesn't have a listen, all clients send to this listen socket.
    if (!socketCfg.m_isUDP && socketCfg.m_isListening) {
      ASSERT(listen(socketFd, MAX_TCP_SERVER_BACKLOG) == 0,
             "listen() failed. errno:" + std::string(strerror(errno)));
    }

    if (socketCfg.m_needsSOTimestamp) {
      ASSERT(setSOTimestamp(socketFd),
             "setSOTimestamp() failed. errno:" + std::string(strerror(errno)));
    }
  }

  return socketFd;
}

} // namespace common
