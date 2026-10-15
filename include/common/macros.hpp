#pragma once

#include <cstdlib>
#include <iostream>
#include <string>

#if !defined(NDEBUG)
#define DCHECK(cond, msg) ASSERT(cond, msg)
#else
#define DCHECK(cond, msg)
#endif

inline void ASSERT(bool cond, const std::string &msg) noexcept {
  if (!cond) [[unlikely]] {
    std::cerr << "ASSERT : " << msg << std::endl;

    exit(EXIT_FAILURE);
  }
}

inline void FATAL(const std::string &msg) noexcept {
  std::cerr << "FATAL : " << msg << std::endl;

  exit(EXIT_FAILURE);
}
