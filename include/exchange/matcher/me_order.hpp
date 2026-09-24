#pragma once

#include <array>

#include "common/constants.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

struct MEOrder {
  TickerId m_tickerId = INVALID_TICKER_ID;
  ClientId m_clientId = INVALID_CLIENT_ID;
  OrderId m_clientOrderId = INVALID_ORDER_ID;
  OrderId m_marketOrderId = INVALID_ORDER_ID;
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;
  Quantity m_qty = INVALID_QUANTITY;
  Priority m_priority = INVALID_PRIORITY;

  MEOrder *m_nextOrder = nullptr;
  MEOrder *m_prevOrder = nullptr;

  // Default constructor defined since we are going to memory pool these order
  // objects.
  MEOrder() = default;

  MEOrder(TickerId tickerId, ClientId clientId, OrderId clientOrderId,
          OrderId marketOrderId, Side side, Price price, Quantity qty,
          Priority priority, MEOrder *nextOrder, MEOrder *prevOrder)
      : m_tickerId(tickerId), m_clientId(clientId),
        m_clientOrderId(clientOrderId), m_marketOrderId(marketOrderId),
        m_side(side), m_price(price), m_qty(qty), m_priority(priority),
        m_nextOrder(nextOrder), m_prevOrder(prevOrder) {
  }

  std::string toString() const;
};

using OrderHashMap = std::array<MEOrder *, ME_MAX_ORDER_IDS>;
using ClientOrderHashMap = std::array<OrderHashMap, ME_MAX_NUM_CLIENTS>;

} // namespace exchange
