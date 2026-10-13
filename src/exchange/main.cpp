#include <atomic>
#include <csignal>
#include <memory>

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "exchange/market_data/market_data_publisher.hpp"
#include "exchange/matcher/matching_engine.hpp"
#include "exchange/order_server/order_server.hpp"

std::unique_ptr<common::Logger> g_logger = nullptr;
std::unique_ptr<exchange::MatchingEngine> g_matchingEngine = nullptr;
std::unique_ptr<exchange::OrderServer> g_orderServer = nullptr;
std::unique_ptr<exchange::MarketDataPublisher> g_marketDataPublisher = nullptr;

std::atomic<bool> g_running{true};

void signalHandler(int) {
  std::cerr
      << "\nSIGINT received. Shutting down matching engine and order server."
      << std::endl;

  g_running.store(false, std::memory_order_relaxed);
}

int main() {
  g_logger = std::make_unique<common::Logger>("exchange_main.log");

  const std::string iface = "lo";
  const int orderServerPort = 5000;
  const std::string incrementalUpdateIp = "239.0.1.1";
  const std::string snapshotIp = "239.0.1.2";
  const int incrementalUpdatePort = 6001;
  const int snapshotPort = 6002;

  g_logger->logInfo("%:% %() Starting matching engine\n", __FILE__, __LINE__,
                    __FUNCTION__);

  g_logger->logInfo("%:% %() Config iface:% orderServerPort:% "
                    "incrementalUpdateIp:% snapshotIp:% "
                    "incrementalUpdatePort:% snapshotPort:%\n",
                    __FILE__, __LINE__, __FUNCTION__, iface, orderServerPort,
                    incrementalUpdateIp, snapshotIp, incrementalUpdatePort,
                    snapshotPort);

  g_logger->logInfo(
      "%:% %() Constants ME_MAX_TICKERS:% ME_MAX_NUM_CLIENTS:% "
      "ME_MAX_CLIENT_UPDATES:% ME_MAX_MARKET_UPDATES:% ME_MAX_ORDER_IDS:% "
      "ME_MAX_PRICE_LEVELS:% MAX_ME_PENDING_REQUESTS:% LOG_QUEUE_SIZE:%\n",
      __FILE__, __LINE__, __FUNCTION__, common::ME_MAX_TICKERS,
      common::ME_MAX_NUM_CLIENTS, common::ME_MAX_CLIENT_UPDATES,
      common::ME_MAX_MARKET_UPDATES, common::ME_MAX_ORDER_IDS,
      common::ME_MAX_PRICE_LEVELS, exchange::MAX_ME_PENDING_REQUESTS,
      common::LOG_QUEUE_SIZE);

  signal(SIGINT, signalHandler);

  exchange::ClientRequestImplQueue clientRequestQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientRequestQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  exchange::ClientResponseImplQueue clientResponseQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientResponseQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  exchange::MarketUpdateImplQueue marketUpdateQueue(ME_MAX_MARKET_UPDATES);
  g_logger->logInfo("%:% %() Created MarketUpdateQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_MARKET_UPDATES);

  g_matchingEngine = std::make_unique<exchange::MatchingEngine>(
      &clientRequestQueue, &clientResponseQueue, &marketUpdateQueue);
  g_matchingEngine->start();
  g_logger->logInfo("%:% %() Starting MatchingEngine thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_orderServer = std::make_unique<exchange::OrderServer>(
      &clientRequestQueue, &clientResponseQueue, iface, orderServerPort);
  g_orderServer->start();
  g_logger->logInfo("%:% %() Starting OrderServer thread\n", __FILE__, __LINE__,
                    __FUNCTION__);

  g_marketDataPublisher = std::make_unique<exchange::MarketDataPublisher>(
      &marketUpdateQueue, iface, snapshotIp, snapshotPort, incrementalUpdateIp,
      incrementalUpdatePort);
  g_marketDataPublisher->start();
  g_logger->logInfo("%:% %() Starting MarketDataPublisher thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_logger->logInfo("%:% %() Exchange up and running, entering main loop\n",
                    __FILE__, __LINE__, __FUNCTION__);

  while (g_running.load(std::memory_order_relaxed)) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  g_logger->logInfo("%:% %() SIGINT received, destroying components\n",
                    __FILE__, __LINE__, __FUNCTION__);
  g_orderServer.reset();
  g_matchingEngine.reset();
  g_marketDataPublisher.reset();

  g_logger->logInfo("%:% %() Destroying Logger\n", __FILE__, __LINE__,
                    __FUNCTION__);
  g_logger.reset();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  return EXIT_SUCCESS;
}
