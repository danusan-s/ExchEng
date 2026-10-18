#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

enum class ClientRequestType : uint8_t {
  INVALID = 0,
  NEW = 1,
  CANCEL = 2,
};

// TODO: Support more request types like MODIFY, etc.
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

struct ClientRequestImpl {
  ClientId m_clientId = INVALID_CLIENT_ID;               // 32 bits
  TickerId m_tickerId = INVALID_TICKER_ID;               // 32 bits
  OrderId m_orderId = INVALID_ORDER_ID;                  // 64 bits
  Price m_price = INVALID_PRICE;                         // 64 bits
  Quantity m_quantity = INVALID_QUANTITY;                // 64 bits
  Side m_side = Side::INVALID;                           // 8 bits
  ClientRequestType m_type = ClientRequestType::INVALID; // 8 bits
  // Total size: 4 + 4 + 8 + 8 + 8 + 1 + 1 = 34 bytes (No padding)

  ClientRequestImpl() = default;

  ClientRequestImpl(ClientRequestType type, ClientId clientId,
                    TickerId tickerId, OrderId orderId, Side side, Price price,
                    Quantity quantity)
      : m_clientId(clientId), m_tickerId(tickerId), m_orderId(orderId),
        m_price(price), m_quantity(quantity), m_side(side), m_type(type) {
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "ClientRequestImpl [ "
        << "type: " << clientRequestTypeToString(m_type)
        << ", clientId: " << clientIdToString(m_clientId)
        << ", tickerId: " << tickerIdToString(m_tickerId)
        << ", orderId: " << orderIdToString(m_orderId)
        << ", side: " << sideToString(m_side)
        << ", price: " << priceToString(m_price)
        << ", quantity: " << quantityToString(m_quantity) << " ]";
    return oss.str();
  }
};

struct ClientRequest {
  size_t m_seqNum = 0;
  ClientRequestImpl m_request;

  std::string toString() const {
    std::ostringstream oss;
    oss << "ClientRequest [ "
        << "seqNum: " << m_seqNum << ", " << m_request.toString() << " ]";
    return oss.str();
  }
};

using ClientRequestImplQueue = common::LFQueue<ClientRequestImpl>;

} // namespace exchange
