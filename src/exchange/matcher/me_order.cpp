#include "exchange/matcher/me_order.hpp"

#include <sstream>

namespace exchange {

std::string MEOrder::toString() const {
  std::ostringstream oss;
  oss << "MEOrder ["
      << "tickerId: " << tickerIdToString(m_tickerId)
      << ", clientId: " << clientIdToString(m_clientId)
      << ", clientOrderId: " << orderIdToString(m_clientOrderId)
      << ", marketOrderId: " << orderIdToString(m_marketOrderId)
      << ", side: " << sideToString(m_side)
      << ", price: " << priceToString(m_price)
      << ", qty: " << quantityToString(m_qty)
      << ", priority: " << priorityToString(m_priority) << ", nextOrder: "
      << (orderIdToString(m_nextOrder ? m_nextOrder->m_marketOrderId
                                      : INVALID_ORDER_ID))
      << ", prevOrder: "
      << (orderIdToString(m_prevOrder ? m_prevOrder->m_marketOrderId
                                      : INVALID_ORDER_ID))
      << "]";

  return oss.str();
}

std::string MEOrdersAtPrice::toString() const {
  std::ostringstream oss;
  oss << "MEOrdersAtPrice ["
      << "side: " << sideToString(m_side)
      << ", price: " << priceToString(m_price) << ", firstOrder: "
      << (orderIdToString(m_firstOrder ? m_firstOrder->m_marketOrderId
                                       : INVALID_ORDER_ID))
      << ", nextEntry: "
      << (priceToString(m_nextEntry ? m_nextEntry->m_price : INVALID_PRICE))
      << ", prevEntry: "
      << (priceToString(m_prevEntry ? m_prevEntry->m_price : INVALID_PRICE))
      << "]";

  return oss.str();
}

} // namespace exchange
