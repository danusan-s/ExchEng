#pragma once

#define ALLOW_PERF_MEASURE

#include <cstdint>

#include "common/time_utils.hpp"

namespace common {

inline auto rdtsc() noexcept {
  unsigned int lo, hi;
  __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

} // namespace common

#ifdef ALLOW_PERF_MEASURE
#define START_MEASURE(TAG) const auto TAG = common::rdtsc()

// do while loop to avoid missing semicolon issues
#define END_MEASURE(TAG, LOGGER)                                               \
  do {                                                                         \
    const auto end = common::rdtsc();                                          \
    LOGGER.logDebug("RDTSC " #TAG " %\n", end - TAG);                          \
  } while (false)

// Wrapper macro just to print the current time in nanoseconds
#define TTT_MEASURE(TAG, LOGGER)                                               \
  do {                                                                         \
    const auto TAG = common::getCurrentNanos();                                \
    LOGGER.logDebug("TTT " #TAG " %\n", TAG);                                  \
  } while (false)

#else

#define START_MEASURE(TAG)
#define END_MEASURE(TAG, LOGGER)
#define TTT_MEASURE(TAG, LOGGER)

#endif
