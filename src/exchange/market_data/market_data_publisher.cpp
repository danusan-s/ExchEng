#include "exchange/market_data/market_data_publisher.hpp"
#include "common/constants.hpp"
#include <cstring>

namespace exchange {
MarketDataPublisher::MarketDataPublisher(MarketUpdateQueue *outgoingUpdateQueue,
                                         const std::string &iface,
                                         const std::string &snapshotIp,
                                         int snapshotPort,
                                         const std::string &incrementalUpdateIp,
                                         int incrementalUpdatePort) noexcept
    : m_outgoingUpdateQueue(outgoingUpdateQueue),
      m_logger("market_data_publisher.log"),
      m_incrementalUpdateSocket(m_logger), m_running(false),
      m_snapshotUpdateQueue(ME_MAX_MARKET_UPDATES) {
  ASSERT(m_incrementalUpdateSocket.init(incrementalUpdateIp, iface,
                                        incrementalUpdatePort, false) >= 0,
         "Failed to initialize incremental update multicast socket. ip:" +
             incrementalUpdateIp +
             " port:" + std::to_string(incrementalUpdatePort) +
             " error:" + std::strerror(errno));
  m_snapshotSynthesizer = new SnapshotSynthesizer(&m_snapshotUpdateQueue, iface,
                                                  snapshotIp, snapshotPort);
}

MarketDataPublisher::~MarketDataPublisher() noexcept {
  stop();
  delete m_snapshotSynthesizer;
  m_snapshotSynthesizer = nullptr;

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);
}

void MarketDataPublisher::start() {
  m_running.store(true, std::memory_order_relaxed);
  m_snapshotSynthesizer->start();

  ASSERT(common::createAndStartThread(-1, "MarketDataPublisher",
                                      [this]() { run(); }) != nullptr,
         "Failed to start MarketDataPublisher thread.");
}

void MarketDataPublisher::stop() {
  m_running.store(false, std::memory_order_relaxed);
  m_snapshotSynthesizer->stop();
}

void MarketDataPublisher::run() noexcept {
  m_logger.logInfo("%:% %() MarketDataPublisher started\n", __FILE__,
                   __LINE__, __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    for (auto marketUpdate = m_outgoingUpdateQueue->getNextToRead();
         m_outgoingUpdateQueue->size() && marketUpdate;
         marketUpdate = m_outgoingUpdateQueue->getNextToRead()) {
      m_logger.logData("%:% %() Publishing market update seq: % %\n",
                       __FILE__, __LINE__, __FUNCTION__, m_nextOutSeqNum,
                       marketUpdate->toString());
      m_incrementalUpdateSocket.send(&m_nextOutSeqNum, sizeof(m_nextOutSeqNum));
      m_incrementalUpdateSocket.send(marketUpdate, sizeof(MEMarketUpdate));
      m_outgoingUpdateQueue->updateReadIndex();

      auto nextSnapshotWrite = m_snapshotUpdateQueue.getNextToWriteTo();
      nextSnapshotWrite->m_seqNum = m_nextOutSeqNum;
      nextSnapshotWrite->m_marketUpdate = *marketUpdate;
      m_snapshotUpdateQueue.updateWriteIndex();

      ++m_nextOutSeqNum;
    }
    m_incrementalUpdateSocket.sendAndRecv();
  }

  m_logger.logInfo("%:% %() MarketDataPublisher stopped\n", __FILE__,
                   __LINE__, __FUNCTION__);
}

} // namespace exchange
