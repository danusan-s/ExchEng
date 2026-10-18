#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;
namespace exchange {

enum class MarketUpdateType : uint8_t {
  INVALID = 0,
  ADD = 1,
  MODIFY = 2,
  CANCEL = 3,
  TRADE = 4,
  CLEAR = 5,
  SNAPSHOT_START = 6,
  SNAPSHOT_END = 7
};

inline const char *marketUpdateTypeToString(MarketUpdateType type) {
  switch (type) {
    case MarketUpdateType::ADD:
      return "ADD";
    case MarketUpdateType::MODIFY:
      return "MODIFY";
    case MarketUpdateType::CANCEL:
      return "CANCEL";
    case MarketUpdateType::TRADE:
      return "TRADE";
    case MarketUpdateType::CLEAR:
      return "CLEAR";
    case MarketUpdateType::SNAPSHOT_START:
      return "SNAPSHOT_START";
    case MarketUpdateType::SNAPSHOT_END:
      return "SNAPSHOT_END";
    case MarketUpdateType::INVALID:
      return "INVALID";
  }
  return "UNKNOWN";
}

struct MarketUpdateImpl {
  TickerId m_tickerId = INVALID_TICKER_ID;             // 32 bit
  Quantity m_qty = INVALID_QUANTITY;                   // 32 bit
  OrderId m_marketOrderId = INVALID_ORDER_ID;          // 64 bit
  Price m_price = INVALID_PRICE;                       // 64 bit
  Priority m_priority = INVALID_PRIORITY;              // 64 bit
  Side m_side = Side::INVALID;                         // 8 bit
  MarketUpdateType m_type = MarketUpdateType::INVALID; // 8 bit
  // Total size = 4 + 4 + 8 + 8 + 8 + 1 + 1 (pad 6) = 40 bytes

  MarketUpdateImpl() = default;

  MarketUpdateImpl(MarketUpdateType type, OrderId marketOrderId,
                   TickerId tickerId, Side side, Price price, Quantity qty,
                   Priority priority)
      : m_tickerId(tickerId), m_qty(qty), m_marketOrderId(marketOrderId),
        m_price(price), m_priority(priority), m_side(side), m_type(type) {
  }

  std::string toString() const {
    std::stringstream ss;
    ss << "MarketUpdateImpl [ "
       << "type: " << marketUpdateTypeToString(m_type)
       << ", ticker: " << tickerIdToString(m_tickerId)
       << ", oid: " << orderIdToString(m_marketOrderId)
       << ", side: " << sideToString(m_side)
       << ", price: " << priceToString(m_price)
       << ", qty: " << quantityToString(m_qty)
       << ", priority: " << priorityToString(m_priority) << " ]";
    return ss.str();
  }
};

struct MarketUpdate {
  size_t m_seqNum = 0;
  MarketUpdateImpl m_marketUpdate;

  std::string toString() const {
    std::stringstream ss;
    ss << "MarketUpdate [ "
       << "seqNum: " << m_seqNum << ", " << m_marketUpdate.toString() << " ]";
    return ss.str();
  }
};

using MarketUpdateImplQueue = common::LFQueue<MarketUpdateImpl>;
using MarketUpdateQueue = common::LFQueue<MarketUpdate>;

} // namespace exchange
