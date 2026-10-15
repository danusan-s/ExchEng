#include "exchange/market_data/market_data_publisher.hpp"
#include "common/constants.hpp"
#include "common/perf_utils.hpp"
#include <cstring>

namespace exchange {
MarketDataPublisher::MarketDataPublisher(
    MarketUpdateImplQueue *outgoingUpdateQueue, const std::string &iface,
    const std::string &snapshotIp, int snapshotPort,
    const std::string &incrementalUpdateIp, int incrementalUpdatePort) noexcept
    : m_outgoingUpdateQueue(outgoingUpdateQueue),
      m_logger("exchange_market_data_publisher.log"),
      m_incrementalUpdateSocket(m_logger), m_running(false),
      m_snapshotUpdateQueue(ME_MAX_MARKET_UPDATES) {
  ASSERT(m_incrementalUpdateSocket.init(incrementalUpdateIp, iface,
                                        incrementalUpdatePort, false) >= 0,
         "Failed to initialize incremental update multicast socket. ip:" +
             incrementalUpdateIp +
             " port:" + std::to_string(incrementalUpdatePort) +
             " error:" + std::strerror(errno));
  m_snapshotSynthesizer = std::make_unique<SnapshotSynthesizer>(
      &m_snapshotUpdateQueue, iface, snapshotIp, snapshotPort);
}

MarketDataPublisher::~MarketDataPublisher() noexcept {
  stop();
  m_snapshotSynthesizer.reset();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);
}

void MarketDataPublisher::start() {
  m_running.store(true, std::memory_order_relaxed);
  m_snapshotSynthesizer->start();

  auto thread = common::createAndStartThread(0, "MarketDataPublisher",
                                             [this]() { run(); });
  ASSERT(thread != nullptr, "Failed to start MarketDataPublisher thread.");
  thread->detach();
}

void MarketDataPublisher::stop() {
  m_running.store(false, std::memory_order_relaxed);
  m_snapshotSynthesizer->stop();
}

void MarketDataPublisher::run() noexcept {
  m_logger.logInfo("%:% %() MarketDataPublisher started\n", __FILE__, __LINE__,
                   __FUNCTION__);

  while (m_running.load(std::memory_order_relaxed)) {
    for (auto marketUpdate = m_outgoingUpdateQueue->getNextToRead();
         marketUpdate != nullptr;
         marketUpdate = m_outgoingUpdateQueue->getNextToRead()) {

      TTT_MEASURE(T5_MarketDataPublisher_LFQueue_read, m_logger);

      m_logger.logData("%:% %() Publishing market update seq: % %\n", __FILE__,
                       __LINE__, __FUNCTION__, m_nextOutSeqNum,
                       marketUpdate->toString());

      START_MEASURE(Exchange_MarketDataPublisher_send);
      m_incrementalUpdateSocket.send(&m_nextOutSeqNum, sizeof(m_nextOutSeqNum));
      m_incrementalUpdateSocket.send(marketUpdate, sizeof(MarketUpdateImpl));
      END_MEASURE(Exchange_MarketDataPublisher_send, m_logger);

      m_outgoingUpdateQueue->updateReadIndex();

      TTT_MEASURE(T6_MarketDataPublisher_UDP_write, m_logger);

      auto nextSnapshotWrite = m_snapshotUpdateQueue.getNextToWriteTo();
      nextSnapshotWrite->m_seqNum = m_nextOutSeqNum;
      nextSnapshotWrite->m_marketUpdate = *marketUpdate;
      m_snapshotUpdateQueue.updateWriteIndex();

      ++m_nextOutSeqNum;
    }
    m_incrementalUpdateSocket.sendAndRecv();
  }

  m_logger.logInfo("%:% %() MarketDataPublisher stopped\n", __FILE__, __LINE__,
                   __FUNCTION__);
}

} // namespace exchange
