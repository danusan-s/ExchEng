#include "exchange/market_data/snapshot_synthesizer.hpp"
#include "common/multicast_socket.hpp"
#include "common/time_utils.hpp"

namespace exchange {
SnapshotSynthesizer::SnapshotSynthesizer(
    MDPMarketUpdateQueue *snapshotUpdateQueue, const std::string &iface,
    const std::string &ip, int port) noexcept
    : m_snapshotUpdateQueue(snapshotUpdateQueue),
      m_logger("snapshot_synthesizer.log"), m_snapshotSocket(m_logger),
      m_orderPool(ME_MAX_ORDER_IDS) {
  ASSERT(m_snapshotSocket.init(ip, iface, port, false) >= 0,
         "Failed to initialize snapshot multicast socket. ip:" + ip + " port:" +
             std::to_string(port) + " error:" + std::strerror(errno));
}

SnapshotSynthesizer::~SnapshotSynthesizer() noexcept {
  stop();
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);
}

void SnapshotSynthesizer::start() {
  m_running.store(true, std::memory_order_relaxed);
  ASSERT(common::createAndStartThread(-1, "SnapshotSynthesizer",
                                      [this]() { run(); }) != nullptr,
         "Failed to start SnapshotSynthesizer thread.");
}

void SnapshotSynthesizer::stop() {
  m_running.store(false, std::memory_order_relaxed);
}

void SnapshotSynthesizer::run() noexcept {
  m_logger.logInfo("%:% %() SnapshotSynthesizer started\n", __FILE__,
                   __LINE__, __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    for (auto snapshotUpdate = m_snapshotUpdateQueue->getNextToRead();
         m_snapshotUpdateQueue->size() && snapshotUpdate;
         snapshotUpdate = m_snapshotUpdateQueue->getNextToRead()) {
      m_logger.logData("%:% %() Processing %\n", __FILE__, __LINE__,
                       __FUNCTION__,
                       snapshotUpdate->toString());
      addToSnapshot(snapshotUpdate);
      m_snapshotUpdateQueue->updateReadIndex();
    }

    if (getCurrentNanos() - m_lastSnapshotTime > 60 * NANOS_TO_SECS) {
      m_lastSnapshotTime = getCurrentNanos();
      publishSnapshot();
    }
  }

  m_logger.logInfo("%:% %() SnapshotSynthesizer stopped\n", __FILE__,
                   __LINE__, __FUNCTION__);
}

void SnapshotSynthesizer::addToSnapshot(
    const MDPMarketUpdate *snapshotUpdate) noexcept {
  const auto &marketUpdate = snapshotUpdate->m_marketUpdate;
  auto &tickerOrders = m_tickerOrders[marketUpdate.m_tickerId];
  switch (marketUpdate.m_type) {
    case MarketUpdateType::ADD: {
      auto order = tickerOrders.at(marketUpdate.m_marketOrderId);
      ASSERT(order == nullptr,
             "Order already exists in snapshot. " + marketUpdate.toString());
      tickerOrders.at(marketUpdate.m_marketOrderId) =
          m_orderPool.allocate(marketUpdate);
    } break;
    case MarketUpdateType::CANCEL: {
      auto order = tickerOrders.at(marketUpdate.m_marketOrderId);
      ASSERT(order != nullptr,
             "Order does not exist in snapshot. " + marketUpdate.toString());
      ASSERT(order->m_marketOrderId == marketUpdate.m_marketOrderId,
             "Order in snapshot has a different order id. " +
                 marketUpdate.toString());
      ASSERT(order->m_side == marketUpdate.m_side,
             "Order in snapshot has a different side. " +
                 marketUpdate.toString());
      m_orderPool.deallocate(order);
      tickerOrders.at(marketUpdate.m_marketOrderId) = nullptr;
    } break;
    case MarketUpdateType::MODIFY: {
      auto order = tickerOrders.at(marketUpdate.m_marketOrderId);
      ASSERT(order != nullptr,
             "Order does not exist in snapshot. " + marketUpdate.toString());
      ASSERT(order->m_marketOrderId == marketUpdate.m_marketOrderId,
             "Order in snapshot has a different order id. " +
                 marketUpdate.toString());
      ASSERT(order->m_side == marketUpdate.m_side,
             "Order in snapshot has a different side. " +
                 marketUpdate.toString());

      order->m_price = marketUpdate.m_price;
      order->m_qty = marketUpdate.m_qty;
    } break;
    case MarketUpdateType::TRADE:
    case MarketUpdateType::CLEAR:
    case MarketUpdateType::SNAPSHOT_START:
    case MarketUpdateType::SNAPSHOT_END:
    case MarketUpdateType::INVALID:
      break;
  }
  ASSERT(
      snapshotUpdate->m_seqNum == m_lastIncrementalSeqNum + 1,
      "Snapshot update seq num is not greater than last incremental seq num. " +
          snapshotUpdate->toString());
  m_lastIncrementalSeqNum = snapshotUpdate->m_seqNum;
}

void SnapshotSynthesizer::publishSnapshot() noexcept {
  size_t snapshotSize = 0;
  const MDPMarketUpdate snapshotStartUpdate{
      snapshotSize++,
      {MarketUpdateType::SNAPSHOT_START, m_lastIncrementalSeqNum,
       INVALID_TICKER_ID, Side::INVALID, INVALID_PRICE, INVALID_QUANTITY,
       INVALID_PRIORITY}};

  m_snapshotSocket.send(&snapshotStartUpdate, sizeof(MDPMarketUpdate));

  for (size_t tickerId = 0; tickerId < m_tickerOrders.size(); ++tickerId) {
    const auto &tickerOrders = m_tickerOrders.at(tickerId);

    MEMarketUpdate clearUpdate;
    clearUpdate.m_type = MarketUpdateType::CLEAR;
    clearUpdate.m_tickerId = tickerId;

    const MDPMarketUpdate snapshotClearUpdate{snapshotSize++, clearUpdate};

    m_snapshotSocket.send(&snapshotClearUpdate, sizeof(MDPMarketUpdate));

    for (size_t orderId = 0; orderId < tickerOrders.size(); ++orderId) {
      const auto *order = tickerOrders.at(orderId);
      if (order != nullptr) {
        const MDPMarketUpdate snapshotUpdate{
            snapshotSize++,
            {MarketUpdateType::ADD, order->m_marketOrderId, order->m_tickerId,
             order->m_side, order->m_price, order->m_qty, order->m_priority}};
        m_snapshotSocket.send(&snapshotUpdate, sizeof(MDPMarketUpdate));
      }
    }
  }

  const MDPMarketUpdate snapshotEndUpdate{
      snapshotSize++,
      {MarketUpdateType::SNAPSHOT_END, m_lastIncrementalSeqNum,
       INVALID_TICKER_ID, Side::INVALID, INVALID_PRICE, INVALID_QUANTITY,
       INVALID_PRIORITY}};
  m_snapshotSocket.send(&snapshotEndUpdate, sizeof(MDPMarketUpdate));

  m_snapshotSocket.sendAndRecv();
  m_logger.logInfo("%:% %() Published snapshot with % updates\n", __FILE__,
                   __LINE__, __FUNCTION__, snapshotSize);
}

} // namespace exchange
