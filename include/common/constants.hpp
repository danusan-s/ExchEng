#pragma once

#include <cstddef>

namespace common {

inline constexpr size_t ME_MAX_TICKERS = 8;

inline constexpr size_t ME_MAX_CLIENT_UPDATES = 256 * 1024;
inline constexpr size_t ME_MAX_MARKET_UPDATES = 256 * 1024;

inline constexpr size_t ME_MAX_NUM_CLIENTS = 256;
inline constexpr size_t ME_MAX_ORDER_IDS = 1024 * 1024;
inline constexpr size_t ME_MAX_PRICE_LEVELS = 256;

} // namespace common
