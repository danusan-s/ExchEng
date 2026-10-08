#pragma once

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/mem_pool.hpp"
#include "common/multicast_socket.hpp"
#include "exchange/market_data/market_update.hpp"

namespace exchange {

class SnapshotSynthesizer final {
public:
  SnapshotSynthesizer(MDPMarketUpdateQueue *snapshotUpdateQueue,
                      const std::string &iface, const std::string &ip,
                      int port) noexcept;

  ~SnapshotSynthesizer() noexcept;

  void start();
  void stop();
  void run() noexcept;

  SnapshotSynthesizer() = delete;
  SnapshotSynthesizer(const SnapshotSynthesizer &) = delete;
  SnapshotSynthesizer &operator=(const SnapshotSynthesizer &) = delete;
  SnapshotSynthesizer(SnapshotSynthesizer &&) = delete;
  SnapshotSynthesizer &operator=(SnapshotSynthesizer &&) = delete;

  void addToSnapshot(const MDPMarketUpdate *snapshotUpdate) noexcept;
  void publishSnapshot() noexcept;

private:
  MDPMarketUpdateQueue *m_snapshotUpdateQueue = nullptr;

  Logger m_logger;

  std::atomic<bool> m_running = {true};

  MulticastSocket m_snapshotSocket;

  std::array<std::array<MEMarketUpdate *, ME_MAX_ORDER_IDS>, ME_MAX_TICKERS>
      m_tickerOrders;

  size_t m_lastIncrementalSeqNum = 0;
  Nanos m_lastSnapshotTime = 0;

  MemPool<MEMarketUpdate> m_orderPool;
};

} // namespace exchange
