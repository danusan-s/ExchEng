#pragma once

#include "common/logging.hpp"

#include "exchange/market_data/market_update.hpp"
#include "exchange/matcher/me_order_book.hpp"
#include "exchange/order_server/client_request.hpp"
#include "exchange/order_server/client_response.hpp"

namespace exchange {

class MatchingEngine final {
public:
  MatchingEngine(ClientRequestQueue *clientRequestQueue,
                 ClientResponseQueue *clientResponseQueue,
                 MarketUpdateQueue *marketUpdateQueue) noexcept;

  ~MatchingEngine() noexcept;

  void start();
  void stop();

  MatchingEngine() = delete;
  MatchingEngine(const MatchingEngine &) = delete;
  MatchingEngine &operator=(const MatchingEngine &) = delete;
  MatchingEngine(MatchingEngine &&) = delete;
  MatchingEngine &operator=(MatchingEngine &&) = delete;

private:
  void run() noexcept;

  OrderBookHashMap m_tickerOrderBooks;

  // Incoming request from the order gateway to the matching engine
  ClientRequestQueue *m_clientRequestQueue = nullptr;

  // Outgoing response from the matching engine to the order gateway
  ClientResponseQueue *m_clientResponseQueue = nullptr;

  // Outgoing market update from the matching engine to the market data gateway
  MarketUpdateQueue *m_marketUpdateQueue = nullptr;

  // Can be accessed by multiple threads so volatile
  std::atomic<bool> m_running = false;

  std::string m_timeStr;
  Logger m_logger;
};

} // namespace exchange
