#pragma once

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/multicast_socket.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"

#include <array>
#include <atomic>
#include <bitset>
#include <map>

namespace trading {

class MarketDataConsumer final {
public:
  MarketDataConsumer(common::ClientId clientId,
                     exchange::MarketUpdateImplQueue *marketUpdateQueue,
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

  void startSnapshotSync(TickerId tickerId);
  void checkSnapshotSync(TickerId tickerId);
  void queueMessage(bool isSnapshot,
                    const exchange::MarketUpdate *marketUpdate);

private:
  std::array<size_t, common::ME_MAX_TICKERS> m_nextIncomingSeqNums;

  // Queue to pass market updates to the trading engine.
  exchange::MarketUpdateImplQueue *m_marketUpdateQueue = nullptr;

  std::atomic<bool> m_running = {false};

  Logger m_logger;
  common::MulticastSocket m_incrementalSocket;
  common::MulticastSocket m_snapshotSocket;

  std::bitset<common::ME_MAX_TICKERS> m_inTickerRecovery;

  const std::string m_iface;
  const std::string m_snapshotIp;
  const int m_snapshotPort;

  using QueuedMarketUpdates =
      std::array<std::map<size_t, exchange::MarketUpdateImpl>,
                 common::ME_MAX_TICKERS>;
  QueuedMarketUpdates m_queuedSnapshotUpdates;
  QueuedMarketUpdates m_queuedIncrementalUpdates;
};

} // namespace trading
