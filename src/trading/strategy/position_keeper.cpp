#include "trading/strategy/position_keeper.hpp"
#include "common/types.hpp"
#include <cstdlib>

namespace trading {

std::string PositionInfo::toString() const {
  std::stringstream ss;
  ss << "Position{"
     << "position: " << m_position << ", realPnL: " << m_realPnL
     << ", unrealPnL: " << m_unrealPnL << ", totalPnL: " << m_totalPnL
     << ", volume: " << m_volume << ", vwaps: ["
     << (m_position ? m_openVWAP[sideToIndex(Side::BUY)] / std::abs(m_position)
                    : 0.0)
     << ", "
     << (m_position ? m_openVWAP[sideToIndex(Side::SELL)] / std::abs(m_position)
                    : 0.0)
     << "] " << (m_bbo ? m_bbo->toString() : "") << "}";
  return ss.str();
}

void PositionInfo::addFill(const exchange::ClientResponseImpl *clientResponse,
                           Logger *logger) noexcept {
  const auto oldPosition = m_position;
  const auto sideIdx = sideToIndex(clientResponse->m_side);
  const auto oppSideIdx =
      sideToIndex(clientResponse->m_side == Side::BUY ? Side::SELL : Side::BUY);
  const auto sideValue = sideToValue(clientResponse->m_side);

  m_position += clientResponse->m_execQty * sideValue;
  m_volume += clientResponse->m_execQty;

  if (oldPosition * sideValue >= 0) {
    // Same direction, update VWAP
    m_openVWAP[sideIdx] += clientResponse->m_execQty * clientResponse->m_price;
  } else {
    const auto oppSideVWAP = m_openVWAP[oppSideIdx] / std::abs(oldPosition);
    m_openVWAP[oppSideIdx] = oppSideVWAP * std::abs(m_position);
    m_realPnL += std::min(static_cast<int32_t>(clientResponse->m_execQty),
                          std::abs(oldPosition)) *
                 (oppSideVWAP - clientResponse->m_price) * sideValue;
    if (m_position * oldPosition < 0) {
      // Position flipped, reset VWAP for the new side
      m_openVWAP[sideIdx] = std::abs(m_position) * clientResponse->m_price;
      m_openVWAP[oppSideIdx] = 0.0;
    }
  }

  if (!m_position) {
    m_openVWAP[sideToIndex(Side::BUY)] = 0.0;
    m_openVWAP[sideToIndex(Side::SELL)] = 0.0;
    m_unrealPnL = 0.0;
  } else {
    if (m_position > 0) {
      m_unrealPnL =
          (clientResponse->m_price -
           (m_openVWAP[sideToIndex(Side::BUY)] / std::abs(m_position))) *
          std::abs(m_position);
    } else {
      m_unrealPnL =
          ((m_openVWAP[sideToIndex(Side::SELL)] / std::abs(m_position)) -
           clientResponse->m_price) *
          std::abs(m_position);
    }
  }

  m_totalPnL = m_realPnL + m_unrealPnL;

  logger->logData("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__, toString(),
                  clientResponse->toString());
}

void PositionInfo::updateBBO(const BBO *bbo, Logger *logger) noexcept {
  m_bbo = bbo;

  if (m_position && bbo->m_bidPrice != INVALID_PRICE &&
      bbo->m_askPrice != INVALID_PRICE) {
    const auto midPrice = (bbo->m_bidPrice + bbo->m_askPrice) * 0.5;
    if (m_position > 0) {
      m_unrealPnL = (midPrice - (m_openVWAP[sideToIndex(Side::BUY)] /
                                 std::abs(m_position))) *
                    std::abs(m_position);
    } else {
      m_unrealPnL =
          ((m_openVWAP[sideToIndex(Side::SELL)] / std::abs(m_position)) -
           midPrice) *
          std::abs(m_position);
    }
    const auto oldTotalPnL = m_totalPnL;
    m_totalPnL = m_realPnL + m_unrealPnL;

    if (m_totalPnL != oldTotalPnL) {
      logger->logData("%:% %() % %\n", __FILE__, __LINE__, __FUNCTION__,
                      toString(), bbo->toString());
    }
  }
}

PositionKeeper::PositionKeeper(common::Logger *logger) : m_logger(logger) {
}

const PositionInfo &PositionKeeper::getPositionInfo(TickerId tickerId) const {
  return m_positions.at(tickerId);
}

std::string PositionKeeper::toString() const {
  double totalRealPnL = 0.0;
  Quantity totalVolume = 0;

  std::stringstream ss;
  ss << "PositionKeeper{\n";
  for (TickerId tickerId = 0; tickerId < m_positions.size(); ++tickerId) {
    ss << "tickerId: " << tickerIdToString(tickerId) << ", "
       << m_positions[tickerId].toString() << "\n";

    totalRealPnL += m_positions[tickerId].m_realPnL;
    totalVolume += m_positions[tickerId].m_volume;
  }
  ss << "Total Real PnL: " << totalRealPnL << ", Total Volume: " << totalVolume
     << "\n";
  ss << "}";
  return ss.str();
}

void PositionKeeper::addFill(
    const exchange::ClientResponseImpl *clientResponse) noexcept {
  m_positions.at(clientResponse->m_tickerId).addFill(clientResponse, m_logger);
}

void PositionKeeper::updateBBO(TickerId tickerId, const BBO *bbo) noexcept {
  m_positions.at(tickerId).updateBBO(bbo, m_logger);
}

} // namespace trading
