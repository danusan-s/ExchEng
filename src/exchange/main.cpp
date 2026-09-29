#include <csignal>

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "exchange/matcher/matching_engine.hpp"

common::Logger *g_logger = nullptr;
exchange::MatchingEngine *g_matchingEngine = nullptr;

void signalHandler(int) {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(10s);

  delete g_logger;
  g_logger = nullptr;
  delete g_matchingEngine;
  g_matchingEngine = nullptr;

  std::this_thread::sleep_for(10s);

  exit(EXIT_SUCCESS);
}

int main() {
  g_logger = new common::Logger("main.log");

  std::string timeStr;

  g_logger->log("%:% %() % Starting matching engine\n", __FILE__, __LINE__,
                __FUNCTION__, common::getCurrentTimeStr(&timeStr));

  signal(SIGINT, signalHandler);

  exchange::ClientRequestQueue clientRequestQueue(ME_MAX_CLIENT_UPDATES);
  exchange::ClientResponseQueue clientResponseQueue(ME_MAX_CLIENT_UPDATES);
  exchange::MarketUpdateQueue marketUpdateQueue(ME_MAX_MARKET_UPDATES);

  g_matchingEngine = new exchange::MatchingEngine(
      &clientRequestQueue, &clientResponseQueue, &marketUpdateQueue);
  g_matchingEngine->start();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return EXIT_SUCCESS;
}
