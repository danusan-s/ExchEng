#include "exchange/market_data/market_update.hpp"
#include "trading/market_data/market_data_consumer.hpp"

using namespace trading;

MarketDataConsumer *g_marketDataConsumer = nullptr;
exchange::MarketUpdateQueue *g_marketUpdateQueue = nullptr;

int main() {
  const std::string iface = "lo";
  const std::string incrementalUpdateIp = "239.0.1.1";
  const std::string snapshotIp = "239.0.1.2";
  const int incrementalUpdatePort = 6001;
  const int snapshotPort = 6002;

  g_marketUpdateQueue = new exchange::MarketUpdateQueue(1024);

  g_marketDataConsumer = new MarketDataConsumer(
      1, g_marketUpdateQueue, iface, snapshotIp, snapshotPort,
      incrementalUpdateIp, incrementalUpdatePort);
  g_marketDataConsumer->start();

  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
}
