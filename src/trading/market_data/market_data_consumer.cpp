#include "trading/market_data/market_data_consumer.hpp"
#include "common/thread_utils.hpp"

namespace trading {

MarketDataConsumer::MarketDataConsumer(
    common::ClientId clientId, exchange::MarketUpdateQueue *marketUpdateQueue,
    const std::string &iface, const std::string &snapshotIp, int snapshotPort,
    const std::string &incrementalIp, int incrementalPort)
    : m_marketUpdateQueue(marketUpdateQueue),
      m_logger("market_data_consumer_" + std::to_string(clientId) + ".log"),
      m_incrementalSocket(m_logger), m_snapshotSocket(m_logger), m_iface(iface),
      m_snapshotIp(snapshotIp), m_snapshotPort(snapshotPort) {

  auto callback = [this](auto socket) { recvCallback(socket); };

  m_incrementalSocket.m_recvCallback = callback;
  ASSERT(m_incrementalSocket.init(incrementalIp, m_iface, incrementalPort,
                                   true) >= 0,
         "Failed to initialize incremental socket. Error: " +
             std::string(strerror(errno)));

  ASSERT(m_incrementalSocket.join(incrementalIp),
         "Failed to join incremental multicast group.");

  m_snapshotSocket.m_recvCallback = callback;
}

MarketDataConsumer::~MarketDataConsumer() {
  stop();
  m_incrementalSocket.leave(m_snapshotIp, m_snapshotPort);
  m_snapshotSocket.leave(m_snapshotIp, m_snapshotPort);

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);
}

void MarketDataConsumer::start() {
  m_running.store(true, std::memory_order_relaxed);
  m_logger.logInfo("Starting MarketDataConsumer.");
  ASSERT(common::createAndStartThread(-1, "MarketDataConsumer",
                                      [this]() { run(); }) != nullptr,
         "Failed to start MarketDataConsumer thread.");
}

void MarketDataConsumer::stop() {
  m_running.store(false, std::memory_order_relaxed);
}

void MarketDataConsumer::run() noexcept {
  m_logger.logInfo("%:% %() MarketDataConsumer started\n", __FILE__, __LINE__,
                   __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    m_incrementalSocket.sendAndRecv();
    m_snapshotSocket.sendAndRecv();
  }

  m_logger.logInfo("%:% %() MarketDataConsumer stopped\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

void MarketDataConsumer::recvCallback(common::MulticastSocket *socket) {
  const auto isSnapshotSocket =
      (socket->m_socketFd == m_snapshotSocket.m_socketFd);

  if (isSnapshotSocket && !m_inRecovery) [[unlikely]] {
    socket->m_nextReceiveValidIndex = 0;
    m_logger.logWarn("%:% %() Received snapshot update while not in recovery. "
                     "Ignoring.\n",
                     __FILE__, __LINE__, __FUNCTION__);
    return;
  }

  if (socket->m_nextReceiveValidIndex >= sizeof(exchange::MDPMarketUpdate)) {
    size_t i = 0;
    for (; i + sizeof(exchange::MDPMarketUpdate) <=
           socket->m_nextReceiveValidIndex;
         i += sizeof(exchange::MDPMarketUpdate)) {
      auto request = reinterpret_cast<exchange::MDPMarketUpdate *>(
          socket->m_inboundData.data() + i);

      m_logger.logData("%:% %() Received % socket with len % : % \n", __FILE__,
                       __LINE__, __FUNCTION__,
                       (isSnapshotSocket ? "snapshot" : "incremental"),
                       sizeof(exchange::MDPMarketUpdate), request->toString());

      const bool alreadyInRecovery = m_inRecovery;
      m_inRecovery =
          (alreadyInRecovery || request->m_seqNum != m_nextIncomingSeqNum);

      if (m_inRecovery) [[unlikely]] {
        if (!alreadyInRecovery) [[unlikely]] {
          m_logger.logWarn(
              "%:% %() Entering recovery mode. Expected seq: % but got seq: "
              "%.\n",
              __FILE__, __LINE__, __FUNCTION__, m_nextIncomingSeqNum,
              request->m_seqNum);
          startSnapshotSync();
        }

        queueMessage(isSnapshotSocket, request);
      } else if (!isSnapshotSocket) {
        ++m_nextIncomingSeqNum;
        auto nextWrite = m_marketUpdateQueue->getNextToWriteTo();
        *nextWrite = std::move(request->m_marketUpdate);
        m_marketUpdateQueue->updateWriteIndex();
      }
    }
    memcpy(socket->m_inboundData.data(), socket->m_inboundData.data() + i,
           socket->m_nextReceiveValidIndex - i);
    socket->m_nextReceiveValidIndex -= i;
  }
}

void MarketDataConsumer::startSnapshotSync() {
  m_queuedSnapshotUpdates.clear();
  m_queuedIncrementalUpdates.clear();

  ASSERT(m_snapshotSocket.init(m_snapshotIp, m_iface, m_snapshotPort, true) >=
             0,
         "Failed to initialize snapshot socket. Error: " +
             std::string(strerror(errno)));

  ASSERT(m_snapshotSocket.join(m_snapshotIp),
         "Failed to join snapshot multicast group.");
}

void MarketDataConsumer::queueMessage(
    bool isSnapshot, const exchange::MDPMarketUpdate *marketUpdate) {
  if (isSnapshot) {
    if (m_queuedSnapshotUpdates.find(marketUpdate->m_seqNum) !=
        m_queuedSnapshotUpdates.end()) {
      m_logger.logWarn(
          "%:% %() Received duplicate snapshot update with seq: %.\n", __FILE__,
          __LINE__, __FUNCTION__, marketUpdate->m_seqNum);
      m_queuedSnapshotUpdates.clear();
    }
    m_queuedSnapshotUpdates[marketUpdate->m_seqNum] =
        marketUpdate->m_marketUpdate;
  } else {
    m_queuedIncrementalUpdates[marketUpdate->m_seqNum] =
        marketUpdate->m_marketUpdate;
  }
  m_logger.logData("%:% %() Size snapshot queue: % incremental queue: %.\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   m_queuedSnapshotUpdates.size(),
                   m_queuedIncrementalUpdates.size());

  checkSnapshotSync();
}

void MarketDataConsumer::checkSnapshotSync() {
  if (m_queuedSnapshotUpdates.empty()) {
    return;
  }

  const auto &firstSnapshotMsg = m_queuedSnapshotUpdates.begin()->second;

  if (firstSnapshotMsg.m_type != exchange::MarketUpdateType::SNAPSHOT_START) {
    m_logger.logWarn(
        "%:% %() First snapshot message is not SNAPSHOT_START. Ignoring "
        "snapshot messages.\n",
        __FILE__, __LINE__, __FUNCTION__);
    m_queuedSnapshotUpdates.clear();
    return;
  }

  std::vector<exchange::MEMarketUpdate> orderedSnapshotUpdates;

  bool haveCompleteSnapshot = true;
  size_t nextExpectedSeqNum = 0;
  for (auto &snapshotItr : m_queuedSnapshotUpdates) {
    if (snapshotItr.first != nextExpectedSeqNum) {
      haveCompleteSnapshot = false;
      m_logger.logWarn(
          "%:% %() Missing snapshot update with seq: %. Ignoring snapshot "
          "messages.\n",
          __FILE__, __LINE__, __FUNCTION__, nextExpectedSeqNum);
      break;
    }

    if (snapshotItr.second.m_type !=
            exchange::MarketUpdateType::SNAPSHOT_START &&
        snapshotItr.second.m_type != exchange::MarketUpdateType::SNAPSHOT_END) {

      orderedSnapshotUpdates.push_back(snapshotItr.second);
    }

    ++nextExpectedSeqNum;
  }

  if (!haveCompleteSnapshot) {
    m_logger.logWarn(
        "%:% %() Incomplete snapshot received. Ignoring snapshot messages.\n",
        __FILE__, __LINE__, __FUNCTION__);
    m_queuedSnapshotUpdates.clear();
    return;
  }

  const auto &lastSnapshotMsg = m_queuedSnapshotUpdates.rbegin()->second;
  if (lastSnapshotMsg.m_type != exchange::MarketUpdateType::SNAPSHOT_END) {
    m_logger.logWarn(
        "%:% %() Last snapshot message is not SNAPSHOT_END. Ignoring snapshot "
        "messages.\n",
        __FILE__, __LINE__, __FUNCTION__);
    m_queuedSnapshotUpdates.clear();
    return;
  }

  bool haveCompleteIncremental = true;
  size_t numIncrementals = 0;
  m_nextIncomingSeqNum = lastSnapshotMsg.m_marketOrderId + 1;
  for (auto &incrementalItr : m_queuedIncrementalUpdates) {
    if (incrementalItr.first < m_nextIncomingSeqNum) {
      continue;
    }
    if (incrementalItr.first != m_nextIncomingSeqNum) {
      m_logger.logWarn(
          "%:% %() Missing incremental update with seq: %. Ignoring "
          "incremental messages.\n",
          __FILE__, __LINE__, __FUNCTION__, m_nextIncomingSeqNum);
      haveCompleteIncremental = false;
      break;
    }
    orderedSnapshotUpdates.push_back(incrementalItr.second);
    ++m_nextIncomingSeqNum;
    ++numIncrementals;
  }

  if (!haveCompleteIncremental) {
    m_logger.logWarn(
        "%:% %() Incomplete incremental updates received. Returning early\n",
        __FILE__, __LINE__, __FUNCTION__);
    m_queuedSnapshotUpdates.clear();
    return;
  }

  for (const auto &itr : orderedSnapshotUpdates) {
    auto nextWrite = m_marketUpdateQueue->getNextToWriteTo();
    *nextWrite = itr;
    m_marketUpdateQueue->updateWriteIndex();
  }

  m_logger.logInfo(
      "%:% %() Snapshot sync complete. Published % snapshot updates and % "
      "incremental updates.\n",
      __FILE__, __LINE__, __FUNCTION__, m_queuedSnapshotUpdates.size() - 2,
      numIncrementals);

  m_queuedSnapshotUpdates.clear();
  m_queuedIncrementalUpdates.clear();
  m_inRecovery = false;

  m_snapshotSocket.leave(m_snapshotIp, m_snapshotPort);
}

} // namespace trading
