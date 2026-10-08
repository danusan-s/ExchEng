#include "common/tcp_socket.hpp"

namespace common {

// Create TCPSocket with provided attributes to either listen-on / connect-to.
auto TCPSocket::connect(const std::string &ip, const std::string &iface,
                        int port, bool isListening) -> int {
  // Note that needsSOTimestamp=true for FIFOSequencer.
  const SocketCfg socketCfg{ip, iface, port, false, isListening, true};
  m_socketFd = createSocket(m_logger, socketCfg);

  m_socketAttrib.sin_addr.s_addr = INADDR_ANY;
  m_socketAttrib.sin_port = htons(port);
  m_socketAttrib.sin_family = AF_INET;

  return m_socketFd;
}

// Called to publish outgoing data from the buffers as well as check for and
// callback if data is available in the read buffers.
auto TCPSocket::sendAndRecv() noexcept -> bool {
  char ctrl[CMSG_SPACE(sizeof(struct timeval))];
  auto cmsg = reinterpret_cast<struct cmsghdr *>(&ctrl);

  iovec iov{m_inboundData.data() + m_nextReceiveValidIndex,
            TCP_BUFFER_SIZE - m_nextReceiveValidIndex};
  msghdr msg{
      &m_socketAttrib, sizeof(m_socketAttrib), &iov, 1, ctrl, sizeof(ctrl), 0};

  // Non-blocking call to read available data.
  const auto readSize = recvmsg(m_socketFd, &msg, MSG_DONTWAIT);
  if (readSize > 0) {
    m_nextReceiveValidIndex += readSize;

    Nanos kernelTime = 0;
    timeval timeKernel;
    if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_TIMESTAMP &&
        cmsg->cmsg_len == CMSG_LEN(sizeof(timeKernel))) {
      memcpy(&timeKernel, CMSG_DATA(cmsg), sizeof(timeKernel));
      kernelTime = timeKernel.tv_sec * NANOS_TO_SECS +
                   timeKernel.tv_usec *
                       NANOS_TO_MICROS; // convert timestamp to nanoseconds.
    }

    const auto userTime = getCurrentNanos();

    m_logger.logData("%:% %() % read socket:% len:% utime:% ktime:% diff:%\n",
                     __FILE__, __LINE__, __FUNCTION__,
                     common::getCurrentTimeStr(&m_timeStr), m_socketFd,
                     m_nextReceiveValidIndex, userTime, kernelTime,
                     (userTime - kernelTime));
    m_recvCallback(this, kernelTime);
  }

  if (m_nextSendValidIndex > 0) {
    // Non-blocking call to send data.
    const auto n = ::send(m_socketFd, m_outboundData.data(),
                          m_nextSendValidIndex, MSG_DONTWAIT | MSG_NOSIGNAL);
    m_logger.logData("%:% %() % send socket:% len:%\n", __FILE__, __LINE__,
                     __FUNCTION__, common::getCurrentTimeStr(&m_timeStr),
                     m_socketFd, n);
  }
  m_nextSendValidIndex = 0;

  return (readSize > 0);
}

/// Write outgoing data to the send buffers.
auto TCPSocket::send(const void *data, size_t len) noexcept -> void {
  memcpy(m_outboundData.data() + m_nextSendValidIndex, data, len);
  m_nextSendValidIndex += len;
}

} // namespace common
