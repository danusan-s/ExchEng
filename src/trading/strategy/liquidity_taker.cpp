#include "trading/strategy/liquidity_taker.hpp"
#include "common/types.hpp"
#include "trading/strategy/trade_engine.hpp"

#include <cmath>

namespace trading {

LiquidityTaker::LiquidityTaker(TradeEngine *tradeEngine,
                               const FeatureEngine *featureEngine,
                               OrderManager *orderManager,
                               const TradeEngineConfigMap &tickerConfig,
                               Logger *logger)
    : m_featureEngine(featureEngine), m_orderManager(orderManager),
      m_logger(logger), m_tickerConfig(tickerConfig) {
  tradeEngine->m_algoOnOrderBookUpdate = [this](auto tickerId, auto price,
                                                auto side, auto orderBook) {
    this->onOrderBookUpdate(tickerId, price, side, orderBook);
  };
  tradeEngine->m_algoOnTradeUpdate = [this](auto marketUpdate, auto orderBook) {
    this->onTradeUpdate(marketUpdate, orderBook);
  };
  tradeEngine->m_algoOnOrderUpdate = [this](auto clientResponse) {
    this->onOrderUpdate(clientResponse);
  };
}

void LiquidityTaker::onOrderBookUpdate(TickerId tickerId, Price price,
                                       Side side,
                                       const MarketOrderBook *) noexcept {
  m_logger->logInfo(
      "%:% %() Received order book update for tickerId=% price=% side=%\n",
      __FILE__, __LINE__, __FUNCTION__, tickerIdToString(tickerId),
      priceToString(price), sideToString(side));
  // Liquidity taker does not place orders on order book updates
}

void LiquidityTaker::onTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                                   const MarketOrderBook *orderBook) noexcept {
  m_logger->logData("%:% %() Received trade update %\n", __FILE__, __LINE__,
                    __FUNCTION__, marketUpdate->toString());
  const auto bbo = orderBook->getBBO();
  const auto aggQtyRatio = m_featureEngine->getAggresiveTradeQtyRatio();

  // NOTE: isnan (not != INVALID_FEATURE): NaN != NaN is always true, so a
  // plain != can never detect the uninitialized ratio. Behavior is unchanged
  // today (NaN >= threshold is false either way), but this states the intent.
  if (bbo->m_bidPrice != INVALID_PRICE && bbo->m_askPrice != INVALID_PRICE &&
      !std::isnan(aggQtyRatio)) [[likely]] {
    m_logger->logData("%:% %() % aggresiveTradeQtyRatio: %\n", __FILE__,
                      __LINE__, __FUNCTION__, bbo->toString(), aggQtyRatio);

    const auto clip = m_tickerConfig.at(marketUpdate->m_tickerId).m_clip;
    const auto threshold =
        m_tickerConfig.at(marketUpdate->m_tickerId).m_threshold;
    if (aggQtyRatio >= threshold) {
      if (marketUpdate->m_side == Side::BUY) {
        m_orderManager->moveOrders(marketUpdate->m_tickerId, bbo->m_askPrice,
                                   INVALID_PRICE, clip);
      } else {
        m_orderManager->moveOrders(marketUpdate->m_tickerId, INVALID_PRICE,
                                   bbo->m_bidPrice, clip);
      }
    }
  }
}

void LiquidityTaker::onOrderUpdate(
    const exchange::MEClientResponse *clientResponse) noexcept {
  m_logger->logData("%:% %() Received order update %\n", __FILE__, __LINE__,
                    __FUNCTION__, clientResponse->toString());
  m_orderManager->onOrderUpdate(clientResponse);
}

} // namespace trading
