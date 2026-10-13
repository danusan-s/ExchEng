#pragma once

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/mem_pool.hpp"
#include "common/multicast_socket.hpp"
#include "exchange/market_data/market_update.hpp"

namespace exchange {

class SnapshotSynthesizer final {
public:
  SnapshotSynthesizer(MarketUpdateQueue *snapshotUpdateQueue,
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

  void addToSnapshot(const MarketUpdate *snapshotUpdate) noexcept;
  void publishSnapshot() noexcept;

  // Testability: expose snapshot state without breaking encapsulation.
  size_t getLastSeqNum() const noexcept {
    return m_lastIncrementalSeqNum;
  }
  const MarketUpdateImpl *getSnapshotOrder(TickerId tickerId,
                                           OrderId orderId) const noexcept {
    return m_tickerOrders.at(tickerId).at(orderId);
  }

private:
  MarketUpdateQueue *m_snapshotUpdateQueue = nullptr;

  Logger m_logger;

  std::atomic<bool> m_running = {true};

  MulticastSocket m_snapshotSocket;

  std::array<std::array<MarketUpdateImpl *, ME_MAX_ORDER_IDS>, ME_MAX_TICKERS>
      m_tickerOrders;

  size_t m_lastIncrementalSeqNum = 0;
  Nanos m_lastSnapshotTime = 0;

  MemPool<MarketUpdateImpl> m_orderPool;
};

} // namespace exchange
