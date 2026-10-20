#include "trading/market_data/market_data_consumer.hpp"
#include "common/perf_utils.hpp"
#include "common/thread_utils.hpp"
#include "common/types.hpp"
#include <sys/socket.h>

namespace trading {

MarketDataConsumer::MarketDataConsumer(
    common::ClientId clientId,
    exchange::MarketUpdateImplQueue *marketUpdateQueue,
    const std::string &iface, const std::string &snapshotIp, int snapshotPort,
    const std::string &incrementalIp, int incrementalPort)
    : m_marketUpdateQueue(marketUpdateQueue),
      m_logger("market_data_consumer_" + std::to_string(clientId) + ".log"),
      m_incrementalSocket(m_logger), m_snapshotSocket(m_logger), m_iface(iface),
      m_snapshotIp(snapshotIp), m_snapshotPort(snapshotPort) {

  m_nextIncomingSeqNums.fill(1);

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
  auto thread = common::createAndStartThread(1, "MarketDataConsumer",
                                             [this]() { run(); });
  ASSERT(thread != nullptr, "Failed to start MarketDataConsumer thread.");
  thread->detach();
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
  TTT_MEASURE(T7_MarketDataConsumer_UDP_read, m_logger);

  START_MEASURE(Exchange_MarketDataConsumer_recvCallback);

  const auto isSnapshotSocket =
      (socket->m_socketFd == m_snapshotSocket.m_socketFd);

  if (isSnapshotSocket && !m_inTickerRecovery.any()) [[unlikely]] {
    m_logger.logWarn("%:% %() Received snapshot update but not in recovery "
                     "mode. Ignoring snapshot update.\n",
                     __FILE__, __LINE__, __FUNCTION__);
    socket->m_nextReceiveValidIndex = 0;
    return;
  }

  if (socket->m_nextReceiveValidIndex >= sizeof(exchange::MarketUpdate)) {
    size_t i = 0;
    for (;
         i + sizeof(exchange::MarketUpdate) <= socket->m_nextReceiveValidIndex;
         i += sizeof(exchange::MarketUpdate)) {
      auto request = reinterpret_cast<exchange::MarketUpdate *>(
          socket->m_inboundData.data() + i);

      m_logger.logData("%:% %() Received % socket with len % : % \n", __FILE__,
                       __LINE__, __FUNCTION__,
                       (isSnapshotSocket ? "snapshot" : "incremental"),
                       sizeof(exchange::MarketUpdate), request->toString());

      const auto tickerId = request->m_marketUpdate.m_tickerId;
      if (tickerId >= ME_MAX_TICKERS) [[unlikely]] {
        m_logger.logError("%:% %() Received update with invalid tickerId: %.\n",
                          __FILE__, __LINE__, __FUNCTION__, tickerId);
        continue;
      }
      const bool alreadyInRecovery = m_inTickerRecovery.test(tickerId);

      if (isSnapshotSocket && !alreadyInRecovery) {
        m_logger.logWarn("%:% %() Received snapshot update but not in recovery "
                         "mode for ticker %. Ignoring snapshot update.\n",
                         __FILE__, __LINE__, __FUNCTION__, tickerId);
        continue;
      }

      m_inTickerRecovery.set(tickerId, alreadyInRecovery ||
                                           request->m_seqNum !=
                                               m_nextIncomingSeqNums[tickerId]);

      if (m_inTickerRecovery.test(tickerId)) [[unlikely]] {
        if (!alreadyInRecovery) [[unlikely]] {
          m_logger.logWarn(
              "%:% %() Entering recovery mode. Expected seq: % but got seq: "
              "%.\n",
              __FILE__, __LINE__, __FUNCTION__, m_nextIncomingSeqNums[tickerId],
              request->m_seqNum);
          startSnapshotSync(tickerId);
        }

        queueMessage(isSnapshotSocket, request);
      } else if (!isSnapshotSocket) {
        ++m_nextIncomingSeqNums[tickerId];
        auto nextWrite = m_marketUpdateQueue->getNextToWriteTo();
        *nextWrite = std::move(request->m_marketUpdate);
        m_marketUpdateQueue->updateWriteIndex();
        TTT_MEASURE(T8_MarketDataConsumer_LFQueue_write, m_logger);
      }
    }
    memcpy(socket->m_inboundData.data(), socket->m_inboundData.data() + i,
           socket->m_nextReceiveValidIndex - i);
    socket->m_nextReceiveValidIndex -= i;
  }

  END_MEASURE(Exchange_MarketDataConsumer_recvCallback, m_logger);
}

void MarketDataConsumer::startSnapshotSync(TickerId tickerId) {
  m_queuedSnapshotUpdates[tickerId].clear();
  m_queuedIncrementalUpdates[tickerId].clear();

  if (m_snapshotSocket.m_socketFd != -1) {
    // Socket already open
    return;
  }

  ASSERT(m_snapshotSocket.init(m_snapshotIp, m_iface, m_snapshotPort, true) >=
             0,
         "Failed to initialize snapshot socket. Error: " +
             std::string(strerror(errno)));

  ASSERT(m_snapshotSocket.join(m_snapshotIp),
         "Failed to join snapshot multicast group.");
}

void MarketDataConsumer::queueMessage(
    bool isSnapshot, const exchange::MarketUpdate *marketUpdate) {
  const auto tickerId = marketUpdate->m_marketUpdate.m_tickerId;
  auto &tickerSnapshotUpdates = m_queuedSnapshotUpdates[tickerId];
  auto &tickerIncrementalUpdates = m_queuedIncrementalUpdates[tickerId];
  if (isSnapshot) {
    if (tickerSnapshotUpdates.find(marketUpdate->m_seqNum) !=
        tickerSnapshotUpdates.end()) {
      m_logger.logWarn(
          "%:% %() Received duplicate snapshot update with seq: %.\n", __FILE__,
          __LINE__, __FUNCTION__, marketUpdate->m_seqNum);
      tickerSnapshotUpdates.clear();
    }
    tickerSnapshotUpdates[marketUpdate->m_seqNum] =
        marketUpdate->m_marketUpdate;
  } else {
    tickerIncrementalUpdates[marketUpdate->m_seqNum] =
        marketUpdate->m_marketUpdate;
  }
  m_logger.logInfo("%:% %() Size snapshot queue: % incremental queue: %.\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   tickerSnapshotUpdates.size(),
                   tickerIncrementalUpdates.size());

  checkSnapshotSync(tickerId);
}

void MarketDataConsumer::checkSnapshotSync(TickerId tickerId) {
  auto &tickerSnapshotUpdates = m_queuedSnapshotUpdates[tickerId];
  auto &tickerIncrementalUpdates = m_queuedIncrementalUpdates[tickerId];
  if (tickerSnapshotUpdates.empty()) {
    return;
  }

  const auto &firstSnapshotMsg = tickerSnapshotUpdates.begin()->second;

  if (firstSnapshotMsg.m_type != exchange::MarketUpdateType::SNAPSHOT_START) {
    m_logger.logWarn(
        "%:% %() First snapshot message is not SNAPSHOT_START. Ignoring "
        "snapshot messages.\n",
        __FILE__, __LINE__, __FUNCTION__);
    tickerSnapshotUpdates.clear();
    return;
  }

  std::vector<exchange::MarketUpdateImpl> orderedSnapshotUpdates;

  bool haveCompleteSnapshot = true;
  size_t nextExpectedSeqNum = 0;
  for (auto &snapshotItr : tickerSnapshotUpdates) {
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
    tickerSnapshotUpdates.clear();
    return;
  }

  const auto &lastSnapshotMsg = tickerSnapshotUpdates.rbegin()->second;
  if (lastSnapshotMsg.m_type != exchange::MarketUpdateType::SNAPSHOT_END) {
    m_logger.logWarn(
        "%:% %() Last snapshot message is not SNAPSHOT_END. Ignoring snapshot "
        "messages.\n",
        __FILE__, __LINE__, __FUNCTION__);
    tickerSnapshotUpdates.clear();
    return;
  }

  bool haveCompleteIncremental = true;
  size_t numIncrementals = 0;
  auto &nextIncomingSeqNum = m_nextIncomingSeqNums.at(tickerId);
  nextIncomingSeqNum = lastSnapshotMsg.m_marketOrderId + 1;
  for (auto &incrementalItr : tickerIncrementalUpdates) {
    if (incrementalItr.first < nextIncomingSeqNum) {
      continue;
    }
    if (incrementalItr.first != nextIncomingSeqNum) {
      m_logger.logWarn(
          "%:% %() Missing incremental update with seq: %. Ignoring "
          "incremental messages.\n",
          __FILE__, __LINE__, __FUNCTION__, nextIncomingSeqNum);
      haveCompleteIncremental = false;
      break;
    }
    orderedSnapshotUpdates.push_back(incrementalItr.second);
    ++nextIncomingSeqNum;
    ++numIncrementals;
  }

  if (!haveCompleteIncremental) {
    m_logger.logWarn(
        "%:% %() Incomplete incremental updates received. Returning early\n",
        __FILE__, __LINE__, __FUNCTION__);
    tickerSnapshotUpdates.clear();
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
      __FILE__, __LINE__, __FUNCTION__, tickerSnapshotUpdates.size() - 2,
      numIncrementals);

  tickerSnapshotUpdates.clear();
  tickerIncrementalUpdates.clear();
  m_inTickerRecovery.reset(tickerId);

  if (!m_inTickerRecovery.any()) {
    m_logger.logInfo(
        "%:% %() All tickers recovered. Leaving snapshot multicast group.\n",
        __FILE__, __LINE__, __FUNCTION__);
    m_snapshotSocket.leave(m_snapshotIp, m_snapshotPort);
  }
}

} // namespace trading
