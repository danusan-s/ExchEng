#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace common {

using OrderId = uint64_t;
constexpr OrderId INVALID_ORDER_ID = std::numeric_limits<OrderId>::max();
inline std::string orderIdToString(OrderId orderId) {
  if (orderId == INVALID_ORDER_ID) {
    return "INVALID_ORDER_ID";
  }
  return std::to_string(orderId);
}

using TickerId = uint32_t;
constexpr TickerId INVALID_TICKER_ID = std::numeric_limits<TickerId>::max();
inline std::string tickerIdToString(TickerId tickerId) {
  if (tickerId == INVALID_TICKER_ID) {
    return "INVALID_TICKER_ID";
  }
  return std::to_string(tickerId);
}

using ClientId = uint32_t;
constexpr ClientId INVALID_CLIENT_ID = std::numeric_limits<ClientId>::max();
inline std::string clientIdToString(ClientId clientId) {
  if (clientId == INVALID_CLIENT_ID) {
    return "INVALID_CLIENT_ID";
  }
  return std::to_string(clientId);
}

using Price = uint64_t;
constexpr Price INVALID_PRICE = std::numeric_limits<Price>::max();
inline std::string priceToString(Price price) {
  if (price == INVALID_PRICE) {
    return "INVALID_PRICE";
  }
  return std::to_string(price);
}

using Quantity = uint32_t;
constexpr Quantity INVALID_QUANTITY = std::numeric_limits<Quantity>::max();
inline std::string quantityToString(Quantity quantity) {
  if (quantity == INVALID_QUANTITY) {
    return "INVALID_QUANTITY";
  }
  return std::to_string(quantity);
}

using Priority = uint64_t;
constexpr Priority INVALID_PRIORITY = std::numeric_limits<Priority>::max();
inline std::string priorityToString(Priority priority) {
  if (priority == INVALID_PRIORITY) {
    return "INVALID_PRIORITY";
  }
  return std::to_string(priority);
}

enum class Side : uint8_t { BUY = 0, SELL = 1, INVALID = 2 };
inline std::string sideToString(Side side) {
  switch (side) {
    case Side::BUY:
      return "BUY";
    case Side::SELL:
      return "SELL";
    case Side::INVALID:
      return "INVALID_SIDE";
  }
  return "UNKNOWN";
}

} // namespace common
