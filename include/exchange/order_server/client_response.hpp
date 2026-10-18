#pragma once

#include <sstream>

#include "common/lf_queue.hpp"
#include "common/types.hpp"

using namespace common;

namespace exchange {

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

struct ClientResponseImpl {
  ClientId m_clientId = INVALID_CLIENT_ID;                 // 32 bits
  TickerId m_tickerId = INVALID_TICKER_ID;                 // 32 bits
  OrderId m_clientOrderId = INVALID_ORDER_ID;              // 64 bits
  OrderId m_marketOrderId = INVALID_ORDER_ID;              // 64 bits
  Price m_price = INVALID_PRICE;                           // 64 bits
  Quantity m_execQty = INVALID_QUANTITY;                   // 64 bits
  Quantity m_leavesQty = INVALID_QUANTITY;                 // 64 bits
  Side m_side = Side::INVALID;                             // 8 bits
  ClientResponseType m_type = ClientResponseType::INVALID; // 8 bits
  // Total size: 4 + 4 + 8 + 8 + 8 + 8 + 8 + 1 + 1 = 50 bytes (No padding)

  ClientResponseImpl() = default;

  ClientResponseImpl(ClientResponseType type, ClientId clientId,
                     TickerId tickerId, OrderId clientOrderId,
                     OrderId marketOrderId, Side side, Price price,
                     Quantity execQty, Quantity leavesQty)
      : m_clientId(clientId), m_tickerId(tickerId),
        m_clientOrderId(clientOrderId), m_marketOrderId(marketOrderId),
        m_price(price), m_execQty(execQty), m_leavesQty(leavesQty),
        m_side(side), m_type(type) {
  }

  std::string toString() const {
    std::ostringstream oss;
    oss << "ClientResponseImpl [ "
        << "type: " << clientResponseTypeToString(m_type)
        << ", clientId: " << clientIdToString(m_clientId)
        << ", tickerId: " << tickerIdToString(m_tickerId)
        << ", orderId: " << orderIdToString(m_clientOrderId)
        << ", marketOrderId: " << orderIdToString(m_marketOrderId)
        << ", side: " << sideToString(m_side)
        << ", price: " << priceToString(m_price)
        << ", quantity: " << quantityToString(m_execQty) << " ]";
    return oss.str();
  }
};

struct ClientResponse {
  size_t m_seqNum = 0;
  ClientResponseImpl m_response;

  std::string toString() const {
    std::ostringstream oss;
    oss << "ClientResponse [ "
        << "seqNum: " << m_seqNum << ", " << m_response.toString() << " ]";
    return oss.str();
  }
};

using ClientResponseImplQueue = common::LFQueue<ClientResponseImpl>;

} // namespace exchange
