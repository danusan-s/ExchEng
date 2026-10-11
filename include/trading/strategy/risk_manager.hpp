#pragma once

#include "common/types.hpp"
#include "trading/strategy/config.hpp"
#include "trading/strategy/position_keeper.hpp"
#include "trading/strategy/trade_engine.hpp"
#include <sstream>

using namespace common;

namespace trading {

class OrderManager;

enum class RiskCheckResult : uint8_t {
  INVALID = 0,
  ORDER_TOO_LARGE = 1,
  POSITION_TOO_LARGE = 2,
  LOSS_TOO_LARGE = 3,
  ALLOWED = 4
};

inline std::string riskCheckResultToString(RiskCheckResult result) {
  switch (result) {
    case RiskCheckResult::INVALID:
      return "INVALID";
    case RiskCheckResult::ORDER_TOO_LARGE:
      return "ORDER_TOO_LARGE";
    case RiskCheckResult::POSITION_TOO_LARGE:
      return "POSITION_TOO_LARGE";
    case RiskCheckResult::LOSS_TOO_LARGE:
      return "LOSS_TOO_LARGE";
    case RiskCheckResult::ALLOWED:
      return "ALLOWED";
  }
  return "UNKNOWN";
}

struct RiskInfo {
  const PositionInfo *m_positionInfo = nullptr;

  RiskConfig m_riskConfig;

  std::string toString() const {
    std::stringstream ss;
    ss << "RiskInfo{positionInfo=" << m_positionInfo->toString()
       << ", riskConfig=" << m_riskConfig.toString() << "}";
    return ss.str();
  }

  RiskCheckResult checkPreTradeRisk(Side side, Quantity qty) const noexcept {
    if (qty > m_riskConfig.m_maxOrderSize) [[unlikely]] {
      return RiskCheckResult::ORDER_TOO_LARGE;
    }

    if (std::abs(m_positionInfo->m_position +
                 (sideToValue(side) * static_cast<int32_t>(qty))) >
        static_cast<int32_t>(m_riskConfig.m_maxPosition)) [[unlikely]] {
      return RiskCheckResult::POSITION_TOO_LARGE;
    }

    if (m_positionInfo->m_totalPnL < m_riskConfig.m_maxLoss) [[unlikely]] {
      return RiskCheckResult::LOSS_TOO_LARGE;
    }

    return RiskCheckResult::ALLOWED;
  }
};

using TickerRishInfoMap = std::array<RiskInfo, ME_MAX_TICKERS>;

class RiskManager final {
public:
  RiskManager(const PositionKeeper *positionKeeper,
              const TradeEngineConfigMap &tickerConfigMap, Logger *logger);

  RiskCheckResult checkPreTradeRisk(TickerId tickerId, Side side,
                                    Quantity qty) const noexcept;

private:
  Logger *m_logger = nullptr;

  TickerRishInfoMap m_tickerRiskInfo;
};

} // namespace trading
