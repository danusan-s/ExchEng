#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>

#include "common/constants.hpp"
#include "common/logging.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "trading/market_data/market_data_consumer.hpp"
#include "trading/order_gateway/order_gateway.hpp"
#include "trading/trader/config.hpp"
#include "trading/trader/trade_engine.hpp"

using namespace trading;

std::unique_ptr<common::Logger> g_logger = nullptr;
std::unique_ptr<TradeEngine> g_tradeEngine = nullptr;
std::unique_ptr<MarketDataConsumer> g_marketDataConsumer = nullptr;
std::unique_ptr<OrderGateway> g_orderGateway = nullptr;

std::atomic<bool> g_running{true};

void signalHandler(int) {
  std::cerr << "\nSIGINT received. Shutting down trading client." << std::endl;

  g_running.store(false, std::memory_order_relaxed);
}

static void shutdown() {
  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  if (g_tradeEngine) {
    g_logger->logInfo("%:% %() Destroying TradeEngine\n", __FILE__, __LINE__,
                      __FUNCTION__);
    g_tradeEngine.reset();
    g_logger->logInfo("%:% %() Destroyed TradeEngine\n", __FILE__, __LINE__,
                      __FUNCTION__);
  }

  if (g_orderGateway) {
    g_logger->logInfo("%:% %() Destroying OrderGateway\n", __FILE__, __LINE__,
                      __FUNCTION__);
    g_orderGateway.reset();
    g_logger->logInfo("%:% %() Destroyed OrderGateway\n", __FILE__, __LINE__,
                      __FUNCTION__);
  }

  if (g_marketDataConsumer) {
    g_logger->logInfo("%:% %() Destroying MarketDataConsumer\n", __FILE__,
                      __LINE__, __FUNCTION__);
    g_marketDataConsumer.reset();
    g_logger->logInfo("%:% %() Destroyed MarketDataConsumer\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  if (g_logger) {
    g_logger->logInfo("%:% %() Destroying Logger\n", __FILE__, __LINE__,
                      __FUNCTION__);
    g_logger.reset();
  }
}

// used to randomly initiate and cancel orders for testing purposes
static void randomAlgo(ClientId clientId) {
  g_logger->logInfo("%:% %() Starting RANDOM order flow\n", __FILE__, __LINE__,
                    __FUNCTION__);
  common::OrderId orderId = clientId * 1000;
  std::vector<exchange::ClientRequestImpl> requests;
  std::array<Price, ME_MAX_TICKERS> prices;
  for (TickerId i = 0; i < ME_MAX_TICKERS; ++i) {
    prices.at(i) = (rand() % 100) + 100;
  }
  g_tradeEngine->initLastEventTime();
  for (size_t i = 0; i < 1000 && g_running.load(std::memory_order_relaxed);
       ++i) {
    g_logger->logInfo("%:% %() RANDOM order flow iteration:%\n", __FILE__,
                      __LINE__, __FUNCTION__, i);
    const TickerId tickerId = rand() % ME_MAX_TICKERS;
    const Price price = prices.at(tickerId) + (rand() % 10) + 1;
    const Quantity qty = (rand() % 100) + 1;
    const Side side = (rand() % 2 == 0) ? Side::BUY : Side::SELL;
    exchange::ClientRequestImpl request{exchange::ClientRequestType::NEW,
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
  g_logger->logInfo("%:% %() Finished RANDOM order flow\n", __FILE__, __LINE__,
                    __FUNCTION__);
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

  g_logger = std::make_unique<common::Logger>(
      "trading_main_" + std::to_string(clientId) + ".log");

  signal(SIGINT, signalHandler);

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

  auto clientRequestsQueue =
      std::make_unique<exchange::ClientRequestImplQueue>(ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientRequestQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  auto clientResponsesQueue =
      std::make_unique<exchange::ClientResponseImplQueue>(
          ME_MAX_CLIENT_UPDATES);
  g_logger->logInfo("%:% %() Created ClientResponseQueue capacity:%\n",
                    __FILE__, __LINE__, __FUNCTION__, ME_MAX_CLIENT_UPDATES);

  auto marketUpdateQueue =
      std::make_unique<exchange::MarketUpdateImplQueue>(ME_MAX_MARKET_UPDATES);
  g_logger->logInfo("%:% %() Created MarketUpdateQueue capacity:%\n", __FILE__,
                    __LINE__, __FUNCTION__, ME_MAX_MARKET_UPDATES);

  g_tradeEngine = std::make_unique<TradeEngine>(
      clientId, algoType, tickerConfig, clientRequestsQueue.get(),
      clientResponsesQueue.get(), marketUpdateQueue.get());
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

  g_orderGateway = std::make_unique<OrderGateway>(
      clientId, clientRequestsQueue.get(), clientResponsesQueue.get(),
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

  g_marketDataConsumer = std::make_unique<MarketDataConsumer>(
      clientId, marketUpdateQueue.get(), iface, snapshotIp, snapshotPort,
      incrementalUpdateIp, incrementalUpdatePort);
  g_marketDataConsumer->start();
  g_logger->logInfo("%:% %() Started MarketDataConsumer thread\n", __FILE__,
                    __LINE__, __FUNCTION__);

  g_logger->logInfo("%:% %() Trading client up and running, warming up\n",
                    __FILE__, __LINE__, __FUNCTION__);

  // Interruptible warmup: 10s total in 100ms slices so SIGINT is honored.
  for (int i = 0; i < 100 && g_running.load(std::memory_order_relaxed); ++i) {
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(100ms);
  }
  g_tradeEngine->initLastEventTime();
  g_logger->logInfo("%:% %() Warmup complete, initialized last event time\n",
                    __FILE__, __LINE__, __FUNCTION__);

  if (algoType == AlgoType::RANDOM) {
    randomAlgo(clientId);
  }

  // Wait till no activity (1s slices so SIGINT is honored promptly).
  g_logger->logInfo("%:% %() Waiting for trade engine to go silent\n", __FILE__,
                    __LINE__, __FUNCTION__);
  while (g_tradeEngine->silentSeconds() < 60 &&
         g_running.load(std::memory_order_relaxed)) {
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(1s);
  }
  if (g_running.load(std::memory_order_relaxed)) {
    g_logger->logInfo("%:% %() Trade engine silent, shutting down\n", __FILE__,
                      __LINE__, __FUNCTION__);
  } else {
    g_logger->logInfo("%:% %() SIGINT received, shutting down\n", __FILE__,
                      __LINE__, __FUNCTION__);
  }

  shutdown();

  using namespace std::literals::chrono_literals;
  std::this_thread::sleep_for(1s);

  exit(g_running.load(std::memory_order_relaxed) ? EXIT_SUCCESS : 130);
}
