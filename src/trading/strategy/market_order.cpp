#include "trading/strategy/market_order.hpp"
#include <sstream>

namespace trading {

std::string MarketOrder::toString() const {
  std::stringstream ss;
  ss << "MarketOrder [ "
     << "orderId: " << orderIdToString(m_orderId)
     << ", side: " << sideToString(m_side)
     << ", price: " << priceToString(m_price)
     << ", quantity: " << quantityToString(m_qty)
     << ", priority: " << priorityToString(m_priority) << " ]";
  return ss.str();
}

std::string MarketOrdersAtPrice::toString() const {
  std::stringstream ss;
  ss << "MaretOrdersAtPrice [ "
     << "side: " << sideToString(m_side)
     << ", price: " << priceToString(m_price)
     << ", firstOrder: " << (m_firstOrder ? m_firstOrder->toString() : "null")
     << ", nextEntry: " << (m_nextEntry ? m_nextEntry->toString() : "null")
     << ", prevEntry: " << (m_prevEntry ? m_prevEntry->toString() : "null")
     << " ]";
  return ss.str();
}

std::string BBO::toString() const {
  std::stringstream ss;
  ss << "BBO [ " << quantityToString(m_bidPrice) << "@"
     << priceToString(m_bidPrice) << "X" << priceToString(m_askPrice) << "@"
     << quantityToString(m_askPrice) << " ]";

  return ss.str();
}

} // namespace trading
