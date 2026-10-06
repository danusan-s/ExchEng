#include "exchange/market_data/snapshot_synthesizer.hpp"
#include "common/multicast_socket.hpp"

namespace exchange {
SnapshotSynthesizer::SnapshotSynthesizer(
    SnapshotUpdateQueue *snapshotUpdateQueue, const std::string &iface,
    const std::string &ip, int port) noexcept
    : m_snapshotUpdateQueue(snapshotUpdateQueue),
      m_logger("snapshot_synthesizer.log"), m_snapshotSocket(m_logger) {
  ASSERT(m_snapshotSocket.init(ip, iface, port, false) >= 0,
         "Failed to initialize snapshot multicast socket. ip:" + ip + " port:" +
             std::to_string(port) + " error:" + std::strerror(errno));
}

} // namespace exchange
