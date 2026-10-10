#pragma once

#include "common/constants.hpp"
#include "common/types.hpp"

using namespace common;

namespace trading {

struct MarketOrder {
  OrderId m_orderId;
  Side m_side;
  Price m_price;
  Quantity m_qty;
  Priority m_priority;

  MarketOrder *m_nextOrder = nullptr;
  MarketOrder *m_prevOrder = nullptr;

  MarketOrder() = default;

  MarketOrder(OrderId orderId, Side side, Price price, Quantity quantity,
              Priority priority, MarketOrder *nextOrder, MarketOrder *prevOrder)
      : m_orderId(orderId), m_side(side), m_price(price), m_qty(quantity),
        m_priority(priority), m_nextOrder(nextOrder), m_prevOrder(prevOrder) {
  }

  std::string toString() const;
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

  std::string toString() const;
};

using OrdersAtPriceHashMap =
    std::array<MarketOrdersAtPrice *, ME_MAX_PRICE_LEVELS>;

// Represents the best bid and offer (BBO) for a given ticker.
struct BBO {
  Price m_bidPrice = INVALID_PRICE;
  Price m_askPrice = INVALID_PRICE;
  Quantity m_bidQty = INVALID_QUANTITY;
  Quantity m_askQty = INVALID_QUANTITY;

  std::string toString() const;
};

} // namespace trading
