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

enum class AlgoType : uint8_t {
  INVALID = 0,
  RANDOM = 1,
  MAKER = 2,
  TAKER = 3,
  MAX = 4
};

inline std::string algoTypeToString(AlgoType algoType) {
  switch (algoType) {
    case AlgoType::INVALID:
      return "INVALID";
    case AlgoType::RANDOM:
      return "RANDOM";
    case AlgoType::MAKER:
      return "MAKER";
    case AlgoType::TAKER:
      return "TAKER";
    case AlgoType::MAX:
      return "MAX";
  }
  return "UNKNOWN";
}

inline AlgoType stringToAlgoType(const std::string &algoTypeStr) {
  for (int i = 0; i < static_cast<int>(AlgoType::MAX); ++i) {
    AlgoType algoType = static_cast<AlgoType>(i);
    if (algoTypeToString(algoType) == algoTypeStr) {
      return algoType;
    }
  }
  return AlgoType::INVALID;
}

}; // namespace trading
