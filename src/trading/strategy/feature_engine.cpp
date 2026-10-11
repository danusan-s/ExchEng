#include "trading/strategy/feature_engine.hpp"
#include "common/types.hpp"

namespace trading {

void FeatureEngine::onOrderBookUpdate(
    TickerId tickerId, Price price, Side side,
    const MarketOrderBook *orderBook) noexcept {
  const auto bbo = orderBook->getBBO();
  if (bbo->m_bidPrice != INVALID_PRICE && bbo->m_askPrice != INVALID_PRICE)
      [[likely]] {
    // Quantity weighted mid price aka micro price
    m_marketPrice =
        (bbo->m_bidPrice * bbo->m_askQty + bbo->m_askPrice * bbo->m_bidQty) /
        static_cast<double>(bbo->m_bidQty + bbo->m_askQty);
  }
  m_logger->logInfo("%:% %() tickerId=% price=% side=% marketPrice=% "
                    "aggresiveTradeQtyRatio=%\n",
                    __FILE__, __LINE__, __FUNCTION__,
                    tickerIdToString(tickerId), priceToString(price),
                    sideToString(side), m_marketPrice,
                    m_aggresiveTradeQtyRatio);
}

void FeatureEngine::onTradeUpdate(const exchange::MEMarketUpdate *marketUpdate,
                                  const MarketOrderBook *orderBook) noexcept {
  const auto bbo = orderBook->getBBO();
  if (bbo->m_bidPrice != INVALID_PRICE && bbo->m_askPrice != INVALID_PRICE)
      [[likely]] {

    // Calculate how aggressive trading is relative to the current BBO qty
    m_aggresiveTradeQtyRatio =
        static_cast<double>(marketUpdate->m_qty) /
        (marketUpdate->m_side == Side::BUY ? bbo->m_askQty : bbo->m_bidQty);
  }

  m_logger->logInfo("%:% %() % marketPrice=% "
                    "aggresiveTradeQtyRatio=%\n",
                    __FILE__, __LINE__, __FUNCTION__, marketUpdate->toString(),
                    m_marketPrice, m_aggresiveTradeQtyRatio);
}

} // namespace trading
