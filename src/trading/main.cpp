#include "common/constants.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "trading/market_data/market_data_consumer.hpp"
#include "trading/order_gateway/order_gateway.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/trade_engine.hpp"

using namespace trading;

common::Logger *g_logger = nullptr;
TradeEngine *g_tradeEngine = nullptr;
MarketDataConsumer *g_marketDataConsumer = nullptr;
OrderGateway *g_orderGateway = nullptr;

int main(int argc, char **argv) {
  const common::ClientId clientId = atoi(argv[1]);
  srand(clientId);
  const AlgoType algoType = stringToAlgoType(argv[2]);

  TradeEngineConfigMap tickerConfig;
  size_t nextTickerId = 0;
  for (int i = 3; i < argc; i += 5, ++nextTickerId) {
    const auto clip = static_cast<Quantity>(atoi(argv[i]));
    const auto threshold = atof(argv[i + 1]);
    const auto maxOrderSize = static_cast<Quantity>(atoi(argv[i + 2]));
    const auto maxPosition = static_cast<Quantity>(atoi(argv[i + 3]));
    const auto maxLoss = atof(argv[i + 4]);

    tickerConfig.at(nextTickerId) = TradeEngineConfig{
        clip, threshold, RiskConfig{maxOrderSize, maxPosition, maxLoss}};
  }

  g_logger =
      new common::Logger("trading_main_" + std::to_string(clientId) + ".log");

  exchange::ClientRequestQueue *clientRequestsQueue =
      new exchange::ClientRequestQueue(ME_MAX_CLIENT_UPDATES);
  exchange::ClientResponseQueue *clientResponsesQueue =
      new exchange::ClientResponseQueue(ME_MAX_CLIENT_UPDATES);
  exchange::MarketUpdateQueue *marketUpdateQueue =
      new exchange::MarketUpdateQueue(ME_MAX_MARKET_UPDATES);

  g_tradeEngine =
      new TradeEngine(clientId, algoType, tickerConfig, clientRequestsQueue,
                      clientResponsesQueue, marketUpdateQueue);
  g_tradeEngine->start();

  const std::string orderServerIp = "127.0.0.1";
  const std::string orderServerIface = "lo";
  const int orderServerPort = 5000;

  g_orderGateway =
      new OrderGateway(clientId, clientRequestsQueue, clientResponsesQueue,
                       orderServerIp, orderServerIface, orderServerPort);
  g_orderGateway->start();

  const std::string iface = "lo";
  const std::string incrementalUpdateIp = "239.0.1.1";
  const std::string snapshotIp = "239.0.1.2";
  const int incrementalUpdatePort = 6001;
  const int snapshotPort = 6002;

  g_marketDataConsumer = new MarketDataConsumer(
      clientId, marketUpdateQueue, iface, snapshotIp, snapshotPort,
      incrementalUpdateIp, incrementalUpdatePort);
  g_marketDataConsumer->start();

  usleep(10 * 1000 * 1000);
  g_tradeEngine->initLastEventTime();

  if (algoType == AlgoType::RANDOM) {
    common::OrderId orderId = clientId * 1000;
    std::vector<exchange::MEClientRequest> requests;
    std::array<Price, ME_MAX_TICKERS> prices;
    for (TickerId i = 0; i < ME_MAX_TICKERS; ++i) {
      prices.at(i) = (rand() % 100) + 100;
    }
    g_tradeEngine->initLastEventTime();
    for (size_t i = 0; i < 10000; ++i) {
      const TickerId tickerId = rand() % ME_MAX_TICKERS;
      const Price price = prices.at(tickerId) + (rand() % 10) + 1;
      const Quantity qty = (rand() % 100) + 1;
      const Side side = (rand() % 2 == 0) ? Side::BUY : Side::SELL;
      exchange::MEClientRequest request{exchange::ClientRequestType::NEW,
                                        clientId,
                                        tickerId,
                                        orderId++,
                                        side,
                                        price,
                                        qty};
      g_tradeEngine->sendClientRequest(&request);
      usleep(20 * 1000);

      requests.push_back(request);

      const auto cancelIndex = rand() % requests.size();
      auto cancelRequest = requests.at(cancelIndex);
      cancelRequest.m_type = exchange::ClientRequestType::CANCEL;
      g_tradeEngine->sendClientRequest(&cancelRequest);
      usleep(20 * 1000);
    }
  }

  // Wait till no activity
  while (g_tradeEngine->silentSeconds() < 60) {
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(10s);
  }

  g_tradeEngine->stop();
  g_orderGateway->stop();
  g_marketDataConsumer->stop();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  delete g_logger;
  g_logger = nullptr;
  delete g_tradeEngine;
  g_tradeEngine = nullptr;
  delete g_orderGateway;
  g_orderGateway = nullptr;
  delete g_marketDataConsumer;
  g_marketDataConsumer = nullptr;

  std::this_thread::sleep_for(1s);

  return 0;
}
