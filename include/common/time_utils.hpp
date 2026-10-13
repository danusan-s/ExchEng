#pragma once

#include <chrono>
#include <cstdio>
#include <string>

namespace common {

using Nanos = int64_t;

constexpr Nanos NANOS_TO_MICROS = 1000;
constexpr Nanos MICROS_TO_MILLIS = 1000;
constexpr Nanos MILLIS_TO_SECS = 1000;
constexpr Nanos NANOS_TO_MILLIS = NANOS_TO_MICROS * MICROS_TO_MILLIS;
constexpr Nanos NANOS_TO_SECS = NANOS_TO_MILLIS * MILLIS_TO_SECS;

inline Nanos getCurrentNanos() noexcept {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

inline std::string &formatTimestamp(int64_t ns, std::string &buf) {
  using namespace std::chrono;

  const auto tp = sys_time<nanoseconds>{nanoseconds{ns}};
  const auto day = floor<days>(tp);
  const year_month_day ymd{day};
  const hh_mm_ss time{tp - day};

  const auto ms = duration_cast<milliseconds>(time.subseconds()).count();

  buf.resize(25); // '[YYYY-MM-DD HH:MM:SS.mmm]' = 25 characters

  std::snprintf(buf.data(), buf.size() + 1,
                "[%04d-%02u-%02u %02lld:%02lld:%02lld.%03lld]", int(ymd.year()),
                unsigned(ymd.month()), unsigned(ymd.day()),
                static_cast<long long>(time.hours().count()),
                static_cast<long long>(time.minutes().count()),
                static_cast<long long>(time.seconds().count()),
                static_cast<long long>(ms));

  return buf;
}

inline std::string &getCurrentTimeStr(std::string &buf) {
  return formatTimestamp(getCurrentNanos(), buf);
}

} // namespace common
