#pragma once

#include <sstream>

#include "common/constants.hpp"
#include "common/types.hpp"

using namespace common;

namespace trading {

enum class OMOrderState : uint8_t {
  INVALID = 0,
  PENDING_NEW = 1,
  LIVE = 2,
  PENDING_CANCEL = 3,
  DEAD = 4
};

inline const char *orderStateToString(OMOrderState state) {
  switch (state) {
    case OMOrderState::INVALID:
      return "INVALID";
    case OMOrderState::PENDING_NEW:
      return "PENDING_NEW";
    case OMOrderState::LIVE:
      return "LIVE";
    case OMOrderState::PENDING_CANCEL:
      return "PENDING_CANCEL";
    case OMOrderState::DEAD:
      return "DEAD";
  }

  return "UNKNOWN";
}

struct OMOrder {
  TickerId m_tickerId = INVALID_TICKER_ID;
  OrderId m_orderId = INVALID_ORDER_ID;
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;
  Quantity m_qty = INVALID_QUANTITY;
  OMOrderState m_state = OMOrderState::INVALID;

  std::string toString() const {
    std::stringstream ss;
    ss << "OMOrder [ "
       << "tickerId: " << tickerIdToString(m_tickerId)
       << ", orderId: " << orderIdToString(m_orderId)
       << ", side: " << sideToString(m_side)
       << ", price: " << priceToString(m_price)
       << ", quantity: " << quantityToString(m_qty)
       << ", state: " << orderStateToString(m_state) << " ]";
    return ss.str();
  }
};

using OMOrderSideMap = std::array<OMOrder, sideToIndex(Side::MAX) + 1>;

using OMOrderTickerSideMap = std::array<OMOrderSideMap, ME_MAX_TICKERS>;

} // namespace trading
