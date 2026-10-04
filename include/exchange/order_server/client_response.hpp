#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

#pragma pack(push, 1)
enum class ClientResponseType : uint8_t {
  INVALID = 0,
  ACCEPTED = 1,
  CANCELED = 2,
  FILLED = 3,
  CANCEL_REJECTED = 4,
};

inline std::string clientResponseTypeToString(ClientResponseType type) {
  switch (type) {
    case ClientResponseType::INVALID:
      return "INVALID_RESPONSE";
    case ClientResponseType::ACCEPTED:
      return "ACCEPTED";
    case ClientResponseType::CANCELED:
      return "CANCELED";
    case ClientResponseType::FILLED:
      return "FILLED";
    case ClientResponseType::CANCEL_REJECTED:
      return "CANCEL_REJECTED";
  }
  return "UNKNOWN";
}

struct MEClientResponse {
  ClientResponseType m_type = ClientResponseType::INVALID;
  ClientId m_clientId = INVALID_CLIENT_ID;
  TickerId m_tickerId = INVALID_TICKER_ID;
  OrderId m_clientOrderId = INVALID_ORDER_ID;
  OrderId m_marketOrderId = INVALID_ORDER_ID;
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;
  Quantity m_execQty = INVALID_QUANTITY;
  Quantity m_leavesQty = INVALID_QUANTITY;

  std::string toString() const {
    std::ostringstream oss;
    oss << "MEClientResponse ["
        << "type: " << clientResponseTypeToString(m_type)
        << ", clientId: " << clientIdToString(m_clientId)
        << ", tickerId: " << tickerIdToString(m_tickerId)
        << ", orderId: " << orderIdToString(m_clientOrderId)
        << ", marketOrderId: " << orderIdToString(m_marketOrderId)
        << ", side: " << sideToString(m_side)
        << ", price: " << priceToString(m_price)
        << ", quantity: " << quantityToString(m_execQty) << "]";
    return oss.str();
  }
};

struct OMClientResponse {
  size_t m_seqNum = 0;
  MEClientResponse m_response;

  std::string toString() const {
    std::ostringstream oss;
    oss << "OMClientResponse ["
        << "seqNum: " << m_seqNum << " " << m_response.toString() << "]";
    return oss.str();
  }
};

#pragma pack(pop)

using ClientResponseQueue = common::LFQueue<MEClientResponse>;

} // namespace exchange
