#pragma once

#include <vector>

#include "common/macros.hpp"

namespace common {

template <typename T> class MemPool final {
public:
  explicit MemPool(std::size_t numElems)
      : m_store(numElems, T()), m_free(numElems, 0) {
    for (size_t i = 0; i < numElems; ++i) {
      m_free[i] = numElems - 1 - i;
    }
  }

  template <typename... Args> T *allocate(Args... args) noexcept {
    DCHECK(!m_free.empty(), "Memory Pool out of space.");

    size_t freeIndex = m_free.back();
    m_free.pop_back();

    T *ret = &(m_store.at(freeIndex));
    if constexpr (!std::is_trivially_destructible_v<T>) {
      ret->~T();
    }
    ret = new (ret) T(args...);

    return ret;
  }

  auto deallocate(const T *elem) noexcept {
    const auto elemIndex = (elem - &m_store.at(0));

    DCHECK(elemIndex >= 0 && static_cast<size_t>(elemIndex) < m_store.size(),
           "Element being deallocated does not belong to this Memory pool.");

    m_free.push_back(elemIndex);
  }

  MemPool() = delete;
  MemPool(const MemPool &) = delete;
  MemPool(MemPool &&) = delete;
  MemPool &operator=(const MemPool &) = delete;
  MemPool &operator=(MemPool &&) = delete;

private:
  std::vector<T> m_store;
  std::vector<size_t> m_free;
};

} // namespace common
