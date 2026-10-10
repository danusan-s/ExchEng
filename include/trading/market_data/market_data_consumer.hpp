#pragma once

#include "common/logging.hpp"
#include "common/multicast_socket.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"

#include <atomic>
#include <map>

namespace trading {

class MarketDataConsumer final {
public:
  MarketDataConsumer(common::ClientId clientId,
                     exchange::MarketUpdateQueue *marketUpdateQueue,
                     const std::string &iface, const std::string &snapshotIp,
                     int snapshotPort, const std::string &incrementalIp,
                     int incrementalPort);

  ~MarketDataConsumer();

  MarketDataConsumer(const MarketDataConsumer &) = delete;
  MarketDataConsumer &operator=(const MarketDataConsumer &) = delete;
  MarketDataConsumer(MarketDataConsumer &&) = delete;
  MarketDataConsumer &operator=(MarketDataConsumer &&) = delete;

  void recvCallback(common::MulticastSocket *socket);

  void start();
  void stop();
  void run() noexcept;

  void startSnapshotSync();
  void checkSnapshotSync();
  void queueMessage(bool isSnapshot,
                    const exchange::MDPMarketUpdate *marketUpdate);

private:
  size_t m_nextIncomingSeqNum = 0;

  // Queue to pass market updates to the trading engine.
  exchange::MarketUpdateQueue *m_marketUpdateQueue = nullptr;

  std::atomic<bool> m_running = {false};

  Logger m_logger;
  common::MulticastSocket m_incrementalSocket;
  common::MulticastSocket m_snapshotSocket;

  bool m_inRecovery = false;

  const std::string m_iface;
  const std::string m_snapshotIp;
  const int m_snapshotPort;

  using QueuedMarketUpdates = std::map<size_t, exchange::MEMarketUpdate>;
  QueuedMarketUpdates m_queuedSnapshotUpdates;
  QueuedMarketUpdates m_queuedIncrementalUpdates;
};

} // namespace trading
