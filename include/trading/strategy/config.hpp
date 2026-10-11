#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"
#include <sstream>

using namespace common;

namespace trading {

struct RiskConfig {
  Quantity m_maxOrderSize = 0;
  Quantity m_maxPosition = 0;
  double m_maxLoss = 0.0;

  std::string toString() const {
    std::stringstream ss;
    ss << "RiskConfig{maxOrderSize=" << quantityToString(m_maxOrderSize)
       << ", maxPosition=" << quantityToString(m_maxPosition)
       << ", maxLoss=" << m_maxLoss << "}";
    return ss.str();
  }
};

struct TradeEngineConfig {
  Quantity m_clip = 0;
  double m_threshold = 0.0;
  RiskConfig m_riskConfig;

  std::string toString() const {
    std::stringstream ss;
    ss << "TradeEngineConfig{clip=" << quantityToString(m_clip)
       << ", threshold=" << m_threshold
       << ", riskConfig=" << m_riskConfig.toString() << "}";
    return ss.str();
  }
};

using TradeEngineConfigMap = std::array<TradeEngineConfig, ME_MAX_TICKERS>;

}; // namespace trading
