#include "trading/trader/risk_manager.hpp"

#include "trading/trader/position_keeper.hpp"

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
  const auto &riskInfo = m_tickerRiskInfo.at(tickerId);
  const auto result = riskInfo.checkPreTradeRisk(side, qty);
  m_logger->logInfo("%:% %() tickerId=% side=% qty=% riskCheckResult=%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    tickerIdToString(tickerId), sideToString(side),
                    quantityToString(qty), riskCheckResultToString(result));
  return result;
}

} // namespace trading
