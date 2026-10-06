#pragma once

#include "common/logging.hpp"
#include "common/multicast_socket.hpp"
#include "exchange/market_data/market_update.hpp"

namespace exchange {

class SnapshotSynthesizer final {
public:
  SnapshotSynthesizer(SnapshotUpdateQueue *snapshotUpdateQueue,
                      const std::string &iface, const std::string &ip,
                      int port) noexcept;

  ~SnapshotSynthesizer() noexcept;

  SnapshotSynthesizer() = delete;
  SnapshotSynthesizer(const SnapshotSynthesizer &) = delete;
  SnapshotSynthesizer &operator=(const SnapshotSynthesizer &) = delete;
  SnapshotSynthesizer(SnapshotSynthesizer &&) = delete;
  SnapshotSynthesizer &operator=(SnapshotSynthesizer &&) = delete;

private:
  SnapshotUpdateQueue *m_snapshotUpdateQueue = nullptr;

  Logger m_logger;
  std::string m_timeStr;

  MulticastSocket m_snapshotSocket;
};

} // namespace exchange
