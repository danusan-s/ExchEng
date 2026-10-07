#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;
namespace exchange {

#pragma pack(push, 1)
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

inline std::string marketUpdateTypeToString(MarketUpdateType type) {
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

struct MEMarketUpdate {
  MarketUpdateType m_type = MarketUpdateType::INVALID;

  OrderId m_marketOrderId = INVALID_ORDER_ID;
  TickerId m_tickerId = INVALID_TICKER_ID;
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;
  Quantity m_qty = INVALID_QUANTITY;
  Priority m_priority = INVALID_PRIORITY;

  std::string toString() const {
    std::stringstream ss;
    ss << "MEMarketUpdate"
       << " ["
       << " type:" << marketUpdateTypeToString(m_type)
       << " ticker:" << tickerIdToString(m_tickerId)
       << " oid:" << orderIdToString(m_marketOrderId)
       << " side:" << sideToString(m_side)
       << " price:" << priceToString(m_price)
       << " qty:" << quantityToString(m_qty)
       << " priority:" << priorityToString(m_priority) << "]";
    return ss.str();
  }
};

struct MDPMarketUpdate {
  size_t m_seqNum = 0;
  MEMarketUpdate m_marketUpdate;

  std::string toString() const {
    std::stringstream ss;
    ss << "MDPMarketUpdate"
       << " ["
       << " seqNum:" << m_seqNum << " " << m_marketUpdate.toString() << "]";
    return ss.str();
  }
};

#pragma pack(pop)

using MarketUpdateQueue = common::LFQueue<MEMarketUpdate>;
using MDPMarketUpdateQueue = common::LFQueue<MDPMarketUpdate>;

} // namespace exchange
