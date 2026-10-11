#include "trading/strategy/risk_manager.hpp"
#include "trading/strategy/position_keeper.hpp"

namespace trading {

RiskManager::RiskManager(const PositionKeeper *positionKeeper,
                         const TradeEngineConfigMap &tradeEngineConfigMap,
                         Logger *logger)
    : m_logger(logger) {
  for (TickerId tickerId = 0; tickerId < ME_MAX_TICKERS; ++tickerId) {
    m_tickerRiskInfo.at(tickerId).m_positionInfo =
        &positionKeeper->getPositionInfo(tickerId);
    m_tickerRiskInfo.at(tickerId).m_riskConfig =
        tradeEngineConfigMap.at(tickerId).m_riskConfig;
  }
}

RiskCheckResult RiskManager::checkPreTradeRisk(TickerId tickerId, Side side,
                                               Quantity qty) const noexcept {
}

} // namespace trading
