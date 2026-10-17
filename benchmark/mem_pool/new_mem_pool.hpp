#pragma once

#include <vector>

#include "common/macros.hpp"

namespace new_common {

template <typename T> class NewMemPool final {
public:
  explicit NewMemPool(std::size_t numElems)
      : m_store(numElems, T()), m_free(numElems, 0) {
    for (size_t i = 0; i < numElems; ++i) {
      m_free[i] = numElems - 1 - i;
    }
  }

  template <typename... Args> T *allocate(Args... args) noexcept {
    DCHECK(!m_free.empty(), "Memory Pool out of space.");

    size_t freeIndex = m_free.back();
    m_free.pop_back();

    DCHECK(freeIndex < m_store.size(),
           "Expected free ObjectBlock at index:" + std::to_string(freeIndex));

    T *ret = &(m_store.at(freeIndex));
    ret = new (ret) T(args...);

    return ret;
  }

  auto deallocate(const T *elem) noexcept {
    const auto elemIndex = elem - &m_store.at(0);

    DCHECK(elemIndex >= 0 && static_cast<size_t>(elemIndex) < m_store.size(),
           "Element being deallocated does not belong to this Memory pool.");

    m_free.push_back(elemIndex);
  }

  NewMemPool() = delete;
  NewMemPool(const NewMemPool &) = delete;
  NewMemPool(NewMemPool &&) = delete;
  NewMemPool &operator=(const NewMemPool &) = delete;
  NewMemPool &operator=(NewMemPool &&) = delete;

private:
  std::vector<T> m_store;
  std::vector<size_t> m_free;
};

} // namespace new_common
