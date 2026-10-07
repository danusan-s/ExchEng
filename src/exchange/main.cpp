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

  delete g_orderServer;
  g_orderServer = nullptr;
  delete g_matchingEngine;
  g_matchingEngine = nullptr;
  delete g_logger;
  g_logger = nullptr;

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  exit(EXIT_SUCCESS);
}

int main() {
  g_logger = new common::Logger("main.log");

  std::string timeStr;
  const std::string iface = "lo";
  const int orderServerPort = 5000;
  const int incrementalUpdatePort = 5001;
  const int snapshotPort = 5002;

  g_logger->log("%:% %() % Starting matching engine\n", __FILE__, __LINE__,
                __FUNCTION__, common::getCurrentTimeStr(&timeStr));

  signal(SIGINT, signalHandler);

  exchange::ClientRequestQueue clientRequestQueue(ME_MAX_CLIENT_UPDATES);
  exchange::ClientResponseQueue clientResponseQueue(ME_MAX_CLIENT_UPDATES);
  exchange::MarketUpdateQueue marketUpdateQueue(ME_MAX_MARKET_UPDATES);

  g_matchingEngine = new exchange::MatchingEngine(
      &clientRequestQueue, &clientResponseQueue, &marketUpdateQueue);
  g_matchingEngine->start();

  g_orderServer = new exchange::OrderServer(
      &clientRequestQueue, &clientResponseQueue, iface, orderServerPort);
  g_orderServer->start();

  g_marketDataPublisher = new exchange::MarketDataPublisher(
      &marketUpdateQueue, iface, "", snapshotPort, "", incrementalUpdatePort);
  g_marketDataPublisher->start();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return EXIT_SUCCESS;
}
