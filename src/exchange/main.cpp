#include <csignal>

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "exchange/market_data/market_data_publisher.hpp"
#include "exchange/matcher/matching_engine.hpp"
#include "exchange/order_server/order_server.hpp"

common::Logger *g_logger = nullptr;
exchange::MatchingEngine *g_matchingEngine = nullptr;
exchange::OrderServer *g_orderServer = nullptr;
exchange::MarketDataPublisher *g_marketDataPublisher = nullptr;

void signalHandler(int) {
  std::cerr
      << "\nSIGINT received. Shutting down matching engine and order server."
      << std::endl;

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  if (g_orderServer) {
    g_logger->logInfo("%:% %() Destroying OrderServer\n", __FILE__, __LINE__,
                      __FUNCTION__);
    delete g_orderServer;
    g_orderServer = nullptr;
    g_logger->logInfo("%:% %() Destroyed OrderServer\n", __FILE__, __LINE__,
                      __FUNCTION__);
  }

  if (g_matchingEngine) {
    g_logger->logInfo("%:% %() Destroying MatchingEngine\n", __FILE__,
                      __LINE__, __FUNCTION__);
    delete g_matchingEngine;
    g_matchingEngine = nullptr;
    g_logger->logInfo("%:% %() Destroyed MatchingEngine\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  if (g_marketDataPublisher) {
    g_logger->logInfo("%:% %() Destroying MarketDataPublisher\n", __FILE__,
                      __LINE__, __FUNCTION__);
    delete g_marketDataPublisher;
    g_marketDataPublisher = nullptr;
    g_logger->logInfo("%:% %() Destroyed MarketDataPublisher\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  g_logger->logInfo("%:% %() Destroying Logger\n", __FILE__, __LINE__,
                    __FUNCTION__);
  delete g_logger;
  g_logger = nullptr;

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  exit(EXIT_SUCCESS);
}

int main() {
  g_logger = new common::Logger("main.log");

  const std::string iface = "lo";
  const int orderServerPort = 5000;
  const int incrementalUpdatePort = 5001;
  const int snapshotPort = 5002;

  g_logger->logInfo("%:% %() Starting matching engine\n", __FILE__, __LINE__,
                    __FUNCTION__);

  g_logger->logInfo("%:% %() Config iface:% orderServerPort:% "
                    "incrementalUpdatePort:% snapshotPort:%\n",
                    __FILE__, __LINE__, __FUNCTION__, iface,
                    orderServerPort, incrementalUpdatePort, snapshotPort);

  g_logger->logInfo(
      "%:% %() Constants ME_MAX_TICKERS:% ME_MAX_NUM_CLIENTS:% "
      "ME_MAX_CLIENT_UPDATES:% ME_MAX_MARKET_UPDATES:% ME_MAX_ORDER_IDS:% "
      "ME_MAX_PRICE_LEVELS:% MAX_ME_PENDING_REQUESTS:% LOG_QUEUE_SIZE:%\n",
      __FILE__, __LINE__, __FUNCTION__,
      common::ME_MAX_TICKERS, common::ME_MAX_NUM_CLIENTS,
      common::ME_MAX_CLIENT_UPDATES, common::ME_MAX_MARKET_UPDATES,
      common::ME_MAX_ORDER_IDS, common::ME_MAX_PRICE_LEVELS,
      exchange::MAX_ME_PENDING_REQUESTS, common::LOG_QUEUE_SIZE);

  signal(SIGINT, signalHandler);

  exchange::ClientRequestQueue clientRequestQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientRequestQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    ME_MAX_CLIENT_UPDATES);

  exchange::ClientResponseQueue clientResponseQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientResponseQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    ME_MAX_CLIENT_UPDATES);

  exchange::MarketUpdateQueue marketUpdateQueue(ME_MAX_MARKET_UPDATES);
  g_logger->logInfo("%:% %() Created MarketUpdateQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    ME_MAX_MARKET_UPDATES);

  g_matchingEngine = new exchange::MatchingEngine(
      &clientRequestQueue, &clientResponseQueue, &marketUpdateQueue);
  g_logger->logInfo("%:% %() Created MatchingEngine\n", __FILE__, __LINE__,
                    __FUNCTION__);
  g_matchingEngine->start();
  g_logger->logInfo("%:% %() Starting MatchingEngine thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_orderServer = new exchange::OrderServer(
      &clientRequestQueue, &clientResponseQueue, iface, orderServerPort);
  g_logger->logInfo("%:% %() Created OrderServer\n", __FILE__, __LINE__,
                    __FUNCTION__);
  g_orderServer->start();
  g_logger->logInfo("%:% %() Starting OrderServer thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_marketDataPublisher = new exchange::MarketDataPublisher(
      &marketUpdateQueue, iface, "", snapshotPort, "", incrementalUpdatePort);
  g_logger->logInfo("%:% %() Created MarketDataPublisher\n", __FILE__,
                    __LINE__, __FUNCTION__);
  g_marketDataPublisher->start();
  g_logger->logInfo("%:% %() Starting MarketDataPublisher thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_logger->logInfo("%:% %() Exchange up and running, entering main loop\n",
                    __FILE__, __LINE__, __FUNCTION__);

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return EXIT_SUCCESS;
}
