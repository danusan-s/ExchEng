#include <cstdlib>

#include "common/constants.hpp"
#include "common/logging.hpp"
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

static void shutdown() {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  if (g_tradeEngine) {
    g_logger->logInfo("%:% %() Destroying TradeEngine\n", __FILE__, __LINE__,
                      __FUNCTION__);
    delete g_tradeEngine;
    g_tradeEngine = nullptr;
    g_logger->logInfo("%:% %() Destroyed TradeEngine\n", __FILE__, __LINE__,
                      __FUNCTION__);
  }

  if (g_orderGateway) {
    g_logger->logInfo("%:% %() Destroying OrderGateway\n", __FILE__, __LINE__,
                      __FUNCTION__);
    delete g_orderGateway;
    g_orderGateway = nullptr;
    g_logger->logInfo("%:% %() Destroyed OrderGateway\n", __FILE__, __LINE__,
                      __FUNCTION__);
  }

  if (g_marketDataConsumer) {
    g_logger->logInfo("%:% %() Destroying MarketDataConsumer\n", __FILE__,
                      __LINE__, __FUNCTION__);
    delete g_marketDataConsumer;
    g_marketDataConsumer = nullptr;
    g_logger->logInfo("%:% %() Destroyed MarketDataConsumer\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  if (g_logger) {
    g_logger->logInfo("%:% %() Destroying Logger\n", __FILE__, __LINE__,
                      __FUNCTION__);
    delete g_logger;
    g_logger = nullptr;
  }
}

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

  g_logger->logInfo("%:% %() Starting trading client\n", __FILE__, __LINE__,
                    __FUNCTION__);

  g_logger->logInfo("%:% %() Config clientId:% algoType:% numTickers:%\n",
                    __FILE__, __LINE__, __FUNCTION__, clientId,
                    algoTypeToString(algoType), nextTickerId);

  for (size_t i = 0; i < nextTickerId; ++i) {
    g_logger->logInfo("%:% %() Ticker:% %\n", __FILE__, __LINE__, __FUNCTION__,
                      i, tickerConfig.at(i).toString());
  }

  g_logger->logInfo(
      "%:% %() Constants ME_MAX_TICKERS:% ME_MAX_NUM_CLIENTS:% "
      "ME_MAX_CLIENT_UPDATES:% ME_MAX_MARKET_UPDATES:% ME_MAX_ORDER_IDS:% "
      "ME_MAX_PRICE_LEVELS:% LOG_QUEUE_SIZE:%\n",
      __FILE__, __LINE__, __FUNCTION__, common::ME_MAX_TICKERS,
      common::ME_MAX_NUM_CLIENTS, common::ME_MAX_CLIENT_UPDATES,
      common::ME_MAX_MARKET_UPDATES, common::ME_MAX_ORDER_IDS,
      common::ME_MAX_PRICE_LEVELS, common::LOG_QUEUE_SIZE);

  exchange::ClientRequestQueue *clientRequestsQueue =
      new exchange::ClientRequestQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientRequestQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  exchange::ClientResponseQueue *clientResponsesQueue =
      new exchange::ClientResponseQueue(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientResponseQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  exchange::MarketUpdateQueue *marketUpdateQueue =
      new exchange::MarketUpdateQueue(ME_MAX_MARKET_UPDATES);
  g_logger->logInfo("%:% %() Created MarketUpdateQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_MARKET_UPDATES);

  g_tradeEngine =
      new TradeEngine(clientId, algoType, tickerConfig, clientRequestsQueue,
                      clientResponsesQueue, marketUpdateQueue);
  g_tradeEngine->start();
  g_logger->logInfo("%:% %() Started TradeEngine thread\n", __FILE__, __LINE__,
                    __FUNCTION__);

  const std::string orderServerIp = "127.0.0.1";
  const std::string orderServerIface = "lo";
  const int orderServerPort = 5000;

  g_logger->logInfo("%:% %() Config orderServerIp:% orderServerIface:% "
                    "orderServerPort:%\n",
                    __FILE__, __LINE__, __FUNCTION__, orderServerIp,
                    orderServerIface, orderServerPort);

  g_orderGateway =
      new OrderGateway(clientId, clientRequestsQueue, clientResponsesQueue,
                       orderServerIp, orderServerIface, orderServerPort);
  g_orderGateway->start();
  g_logger->logInfo("%:% %() Started OrderGateway thread\n", __FILE__, __LINE__,
                    __FUNCTION__);

  const std::string iface = "lo";
  const std::string incrementalUpdateIp = "239.0.1.1";
  const std::string snapshotIp = "239.0.1.2";
  const int incrementalUpdatePort = 6001;
  const int snapshotPort = 6002;

  g_logger->logInfo("%:% %() Config iface:% snapshotIp:% snapshotPort:% "
                    "incrementalUpdateIp:% incrementalUpdatePort:%\n",
                    __FILE__, __LINE__, __FUNCTION__, iface, snapshotIp,
                    snapshotPort, incrementalUpdateIp, incrementalUpdatePort);

  g_marketDataConsumer = new MarketDataConsumer(
      clientId, marketUpdateQueue, iface, snapshotIp, snapshotPort,
      incrementalUpdateIp, incrementalUpdatePort);
  g_marketDataConsumer->start();
  g_logger->logInfo("%:% %() Started MarketDataConsumer thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_logger->logInfo("%:% %() Trading client up and running, warming up\n",
                    __FILE__, __LINE__, __FUNCTION__);

  usleep(10 * 1000 * 1000);
  g_tradeEngine->initLastEventTime();
  g_logger->logInfo("%:% %() Warmup complete, initialized last event time\n",
                    __FILE__, __LINE__, __FUNCTION__);

  if (algoType == AlgoType::RANDOM) {
    g_logger->logInfo("%:% %() Starting RANDOM order flow\n", __FILE__,
                      __LINE__, __FUNCTION__);
    common::OrderId orderId = clientId * 1000;
    std::vector<exchange::MEClientRequest> requests;
    std::array<Price, ME_MAX_TICKERS> prices;
    for (TickerId i = 0; i < ME_MAX_TICKERS; ++i) {
      prices.at(i) = (rand() % 100) + 100;
    }
    g_tradeEngine->initLastEventTime();
    for (size_t i = 0; i < 1000; ++i) {
      g_logger->logInfo("%:% %() RANDOM order flow iteration:%\n", __FILE__,
                        __LINE__, __FUNCTION__, i);
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
    g_logger->logInfo("%:% %() Finished RANDOM order flow\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  // Wait till no activity
  g_logger->logInfo("%:% %() Waiting for trade engine to go silent\n", __FILE__,
                    __LINE__, __FUNCTION__);
  while (g_tradeEngine->silentSeconds() < 60) {
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(10s);
  }
  g_logger->logInfo("%:% %() Trade engine silent, shutting down\n", __FILE__,
                    __LINE__, __FUNCTION__);

  shutdown();
  delete clientRequestsQueue;
  delete clientResponsesQueue;
  delete marketUpdateQueue;

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  exit(EXIT_SUCCESS);
}
