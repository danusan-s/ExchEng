#pragma once

#include <sstream>

#include "common/constants.hpp"
#include "common/types.hpp"

using namespace common;

namespace trading {

struct MarketOrder {
  OrderId m_orderId;   // 64 bit
  Price m_price;       // 64 bit
  Priority m_priority; // 64 bit
  Quantity m_qty;      // 32 bit
  Side m_side;         // 8 bit

  MarketOrder *m_nextOrder = nullptr;
  MarketOrder *m_prevOrder = nullptr;

  // Total size = 8 + 8 + 8 + 4 + 1 (pad 3) + 8 + 8 = 48

  MarketOrder() = default;

  MarketOrder(OrderId orderId, Side side, Price price, Quantity quantity,
              Priority priority, MarketOrder *nextOrder, MarketOrder *prevOrder)
      : m_orderId(orderId), m_price(price), m_priority(priority),
        m_qty(quantity), m_side(side), m_nextOrder(nextOrder),
        m_prevOrder(prevOrder) {
  }

  std::string toString() const {
    std::stringstream ss;
    ss << "MarketOrder [ "
       << "orderId: " << orderIdToString(m_orderId)
       << ", side: " << sideToString(m_side)
       << ", price: " << priceToString(m_price)
       << ", quantity: " << quantityToString(m_qty)
       << ", priority: " << priorityToString(m_priority) << " ]";
    return ss.str();
  }
};

using OrderHashMap = std::array<MarketOrder *, ME_MAX_ORDER_IDS>;

struct MarketOrdersAtPrice {
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;

  MarketOrder *m_firstOrder = nullptr;

  MarketOrdersAtPrice *m_nextEntry = nullptr;
  MarketOrdersAtPrice *m_prevEntry = nullptr;

  MarketOrdersAtPrice() = default;

  MarketOrdersAtPrice(Side side, Price price, MarketOrder *firstOrder,
                      MarketOrdersAtPrice *nextEntry,
                      MarketOrdersAtPrice *prevEntry)
      : m_side(side), m_price(price), m_firstOrder(firstOrder),
        m_nextEntry(nextEntry), m_prevEntry(prevEntry) {
  }

  std::string toString() const {
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
};

using OrdersAtPriceHashMap =
    std::array<MarketOrdersAtPrice *, ME_MAX_PRICE_LEVELS>;

// Represents the best bid and offer (BBO) for a given ticker.
struct BBO {
  Price m_bidPrice = INVALID_PRICE;
  Price m_askPrice = INVALID_PRICE;
  Quantity m_bidQty = INVALID_QUANTITY;
  Quantity m_askQty = INVALID_QUANTITY;

  std::string toString() const {
    std::stringstream ss;
    ss << "BBO [ " << quantityToString(m_bidPrice) << "@"
       << priceToString(m_bidPrice) << "X" << priceToString(m_askPrice) << "@"
       << quantityToString(m_askPrice) << " ]";

    return ss.str();
  }
};

} // namespace trading
