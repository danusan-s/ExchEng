#include "trading/strategy/market_maker.hpp"
#include "trading/strategy/feature_engine.hpp"
#include "trading/strategy/trade_engine.hpp"

#include <cmath>

namespace trading {

MarketMaker::MarketMaker(TradeEngine *tradeEngine,
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

void MarketMaker::onOrderBookUpdate(TickerId tickerId, Price price, Side side,
                                    const MarketOrderBook *orderBook) noexcept {
  m_logger->logInfo(
      "%:% %() Received order book update for tickerId=% price=% side=%\n",
      __FILE__, __LINE__, __FUNCTION__, tickerIdToString(tickerId),
      priceToString(price), sideToString(side));

  const auto bbo = orderBook->getBBO();
  const auto fairPrice = m_featureEngine->getMarketPrice();
  // NOTE: must use isnan (not != INVALID_FEATURE): NaN != NaN is always true,
  // so a plain != comparison can never detect the uninitialized feature and
  // would quote off a NaN fair value. The book's != check has this bug.
  if (bbo->m_bidPrice != INVALID_PRICE && bbo->m_askPrice != INVALID_PRICE &&
      !std::isnan(fairPrice)) [[likely]] {
    m_logger->logInfo("%:% %() % fairPrice: %\n", __FILE__, __LINE__,
                      __FUNCTION__, bbo->toString(), fairPrice);

    const auto clip = m_tickerConfig.at(tickerId).m_clip;
    const auto threshold = m_tickerConfig.at(tickerId).m_threshold;

    // If fair price higher than current best bid by threshold
    // then it is under valued so we hold our order at best bid
    // else we lower to buy at a better price
    const auto bidPrice =
        bbo->m_bidPrice - (fairPrice - bbo->m_bidPrice >= threshold ? 0 : 1);
    // If fair price lower than current best ask by m_threshold
    // then it is over valued so we hold our order at best ask
    // else we raise to sell at a better price
    const auto askPrice =
        bbo->m_askPrice + (bbo->m_askPrice - fairPrice >= threshold ? 0 : 1);

    m_orderManager->moveOrders(tickerId, bidPrice, askPrice, clip);
  }
}

void MarketMaker::onTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                                const MarketOrderBook *) noexcept {
  m_logger->logInfo("%:% %() Received trade update %\n", __FILE__, __LINE__,
                    __FUNCTION__, marketUpdate->toString());
  // Market maker does not react to trade updates.
}

void MarketMaker::onOrderUpdate(
    const exchange::MEClientResponse *clientResponse) noexcept {
  m_logger->logInfo("%:% %() Received order update %\n", __FILE__, __LINE__,
                    __FUNCTION__, clientResponse->toString());
  m_orderManager->onOrderUpdate(clientResponse);
}

} // namespace trading
