#pragma once

#include "common/logging.hpp"
#include "common/types.hpp"
#include "trading/trader/market_order_book.hpp"

using namespace common;

namespace trading {

constexpr auto INVALID_FEATURE = std::numeric_limits<double>::quiet_NaN();

class FeatureEngine final {
public:
  FeatureEngine(Logger *logger) : m_logger(logger) {
  }

  FeatureEngine() = delete;
  FeatureEngine(const FeatureEngine &) = delete;
  FeatureEngine(FeatureEngine &&) = delete;
  FeatureEngine &operator=(const FeatureEngine &) = delete;
  FeatureEngine &operator=(FeatureEngine &&) = delete;

  double getMarketPrice() const noexcept {
    return m_marketPrice;
  }

  double getAggresiveTradeQtyRatio() const noexcept {
    return m_aggresiveTradeQtyRatio;
  }

  void onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                         const MarketOrderBook *orderBook) noexcept;

  void onTradeUpdate(const exchange::MarketUpdateImpl *marketUpdate,
                     const MarketOrderBook *orderBook) noexcept;

private:
  Logger *m_logger = nullptr;

  double m_marketPrice = INVALID_FEATURE;
  double m_aggresiveTradeQtyRatio = INVALID_FEATURE;
};

} // namespace trading
