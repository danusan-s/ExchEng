#pragma once

#include <array>

#include "common/logging.hpp"
#include "common/types.hpp"
#include "exchange/order_server/client_response.hpp"
#include "trading/trader/market_order_book.hpp"

using namespace common;

namespace trading {

struct PositionInfo {
  // Current position
  int32_t m_position = 0;
  // Realized PnL for closed positions
  double m_realPnL = 0.0;
  // Unrealized PnL for open positions
  double m_unrealPnL = 0.0;
  // Total PnL (realized + unrealized)
  double m_totalPnL = 0.0;
  // Volume Weighted Average Price (VWAP) for open positions
  std::array<double, sideToIndex(Side::MAX) + 1> m_openVWAP{};
  // Total qty that has been executed
  Quantity m_volume = 0;
  const BBO *m_bbo = nullptr;

  std::string toString() const;
  void addFill(const exchange::ClientResponseImpl *clientResponse,
               Logger *logger) noexcept;
  void updateBBO(const BBO *bbo, Logger *logger) noexcept;
};

class PositionKeeper final {
public:
  PositionKeeper(common::Logger *logger);

  PositionKeeper(const PositionKeeper &) = delete;
  PositionKeeper &operator=(const PositionKeeper &) = delete;
  PositionKeeper(PositionKeeper &&) = delete;
  PositionKeeper &operator=(PositionKeeper &&) = delete;

  const PositionInfo &getPositionInfo(TickerId tickerId) const;
  std::string toString() const;
  void addFill(const exchange::ClientResponseImpl *clientResponse) noexcept;
  void updateBBO(TickerId tickerId, const BBO *bbo) noexcept;

private:
  common::Logger *m_logger = nullptr;
  std::array<PositionInfo, ME_MAX_TICKERS> m_positions;
};

} // namespace trading
