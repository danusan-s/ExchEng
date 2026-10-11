#pragma once

#include "common/logging.hpp"
#include "common/types.hpp"
#include "exchange/market_data/market_update.hpp"
#include "exchange/order_server/client_request.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/market_order_book.hpp"
#include "trading/strategy/risk_manager.hpp"

namespace trading {

class TradeEngine final {
public:
  void onTrade(const exchange::MEMarketUpdate *marketUpdate,
               MarketOrderBook *orderBook) noexcept;
  void onOrderBookUpdate(TickerId tickerId, Price price, Side side) noexcept;
  ClientId getClientId() const noexcept;

  void
  sendClientRequest(const exchange::MEClientRequest *clientRequest) noexcept;

private:
};

} // namespace trading
