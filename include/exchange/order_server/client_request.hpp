#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

// Tightly pack these because these will be sent over the network
// we want to minimize the size of the message.
#pragma pack(push, 1)
enum class ClientRequestType : uint8_t {
  INVALID = 0,
  NEW = 1,
  CANCEL = 2,
};

inline std::string clientRequestTypeToString(ClientRequestType type) {
  switch (type) {
    case ClientRequestType::INVALID:
      return "INVALID_REQUEST";
    case ClientRequestType::NEW:
      return "NEW";
    case ClientRequestType::CANCEL:
      return "CANCEL";
  }
  return "UNKNOWN";
}

struct MEClientRequest {
  ClientRequestType m_type = ClientRequestType::INVALID;
  ClientId m_clientId = INVALID_CLIENT_ID;
  TickerId m_tickerId = INVALID_TICKER_ID;
  OrderId m_orderId = INVALID_ORDER_ID;
  Side m_side = Side::INVALID;
  Price m_price = INVALID_PRICE;
  Quantity m_quantity = INVALID_QUANTITY;

  std::string toString() const {
    std::ostringstream oss;
    oss << "MEClientRequest ["
        << "type: " << clientRequestTypeToString(m_type)
        << ", clientId: " << clientIdToString(m_clientId)
        << ", tickerId: " << tickerIdToString(m_tickerId)
        << ", orderId: " << orderIdToString(m_orderId)
        << ", side: " << sideToString(m_side)
        << ", price: " << priceToString(m_price)
        << ", quantity: " << quantityToString(m_quantity) << "]";
    return oss.str();
  }
};

#pragma pack(pop)

using MEClientRequestQueue = common::LFQueue<MEClientRequest>;

} // namespace exchange
