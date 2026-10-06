#pragma once

#include "common/logging.hpp"
#include "common/multicast_socket.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/market_data/snapshot_synthesizer.hpp"

using namespace common;

namespace exchange {

class MarketDataPublisher final {
public:
  MarketDataPublisher(MarketUpdateQueue *outgoingUpdateQueue,
                      const std::string &iface, const std::string &snapshotIp,
                      int snapshotPort, const std::string &incrementalUpdateIp,
                      int incrementalUpdatePort) noexcept;

  ~MarketDataPublisher() noexcept;

  void start();
  void stop();
  void run() noexcept;

  MarketDataPublisher() = delete;
  MarketDataPublisher(const MarketDataPublisher &) = delete;
  MarketDataPublisher &operator=(const MarketDataPublisher &) = delete;
  MarketDataPublisher(MarketDataPublisher &&) = delete;
  MarketDataPublisher &operator=(MarketDataPublisher &&) = delete;

private:
  size_t m_nextOutSeqNum = 1;
  MarketUpdateQueue *m_outgoingUpdateQueue = nullptr;

  std::string m_timeStr;
  Logger m_logger;

  MulticastSocket m_incrementalUpdateSocket;

  std::atomic<bool> m_running = false;

  SnapshotUpdateQueue m_snapshotUpdateQueue;
  SnapshotSynthesizer *m_snapshotSynthesizer = nullptr;
};

} // namespace exchange
