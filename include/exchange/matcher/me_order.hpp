#pragma once

#include <array>

#include "common/constants.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

struct MEOrder {
  TickerId m_tickerId = INVALID_TICKER_ID;    // 32 bit -> 4 byte
  ClientId m_clientId = INVALID_CLIENT_ID;    // 32 bit
  OrderId m_clientOrderId = INVALID_ORDER_ID; // 64 bit -> 8 byte
  OrderId m_marketOrderId = INVALID_ORDER_ID; // 64 bit
  Price m_price = INVALID_PRICE;              // 64 bit
  Priority m_priority = INVALID_PRIORITY;     // 64 bit
  Quantity m_qty = INVALID_QUANTITY;          // 32 bit
  Side m_side = Side::INVALID;                // 8 bit

  MEOrder *m_nextOrder = nullptr;
  MEOrder *m_prevOrder = nullptr;

  // Total size = 4 + 4 + 8 + 8 + 8 + 8 + 4 + 1 (pad 3) + 8 + 8 = 64 bytes

  // Default constructor defined since we are going to memory pool these order
  // objects.
  MEOrder() = default;

  MEOrder(TickerId tickerId, ClientId clientId, OrderId clientOrderId,
          OrderId marketOrderId, Side side, Price price, Quantity qty,
          Priority priority, MEOrder *nextOrder, MEOrder *prevOrder)
      : m_tickerId(tickerId), m_clientId(clientId),
        m_clientOrderId(clientOrderId), m_marketOrderId(marketOrderId),
        m_price(price), m_priority(priority), m_qty(qty), m_side(side),
        m_nextOrder(nextOrder), m_prevOrder(prevOrder) {
  }

  std::string toString() const;
};

using OrderHashMap = std::array<MEOrder *, ME_MAX_ORDER_IDS>;
using ClientOrderHashMap = std::array<OrderHashMap, ME_MAX_NUM_CLIENTS>;

// A linked list holding MEOrder linked lists for a given price and side.
struct MEOrdersAtPrice {
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;

  MEOrder *m_firstOrder = nullptr;

  MEOrdersAtPrice *m_nextEntry = nullptr;
  MEOrdersAtPrice *m_prevEntry = nullptr;

  MEOrdersAtPrice() = default;

  MEOrdersAtPrice(Side side, Price price, MEOrder *firstOrder,
                  MEOrdersAtPrice *nextEntry, MEOrdersAtPrice *prevEntry)
      : m_side(side), m_price(price), m_firstOrder(firstOrder),
        m_nextEntry(nextEntry), m_prevEntry(prevEntry) {
  }

  std::string toString() const;
};

using OrdersAtPriceHashMap = std::array<MEOrdersAtPrice *, ME_MAX_PRICE_LEVELS>;

} // namespace exchange
