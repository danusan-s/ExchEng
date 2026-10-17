#pragma once

#include <list>
#include <string>
#include <vector>

#include "common/macros.hpp"

namespace new_common {

template <typename T> class NewMemPool final {
public:
  explicit NewMemPool(std::size_t numElems)
      : m_store(numElems, T()), m_free(numElems, true) {
  }

  template <typename... Args> T *allocate(Args... args) noexcept {

    DCHECK(m_free.at(m_nextFreeIndex), "Expected free ObjectBlock at index:" +
                                           std::to_string(m_nextFreeIndex));

    T *ret = &(m_store.at(m_nextFreeIndex));
    ret = new (ret) T(args...);
    m_free.at(m_nextFreeIndex) = false;

    updateNextFreeIndex();

    return ret;
  }

  auto deallocate(const T *elem) noexcept {
    const auto elemIndex = elem - &m_store.at(0);

    DCHECK(elemIndex >= 0 && static_cast<size_t>(elemIndex) < m_store.size(),
           "Element being deallocated does not belong to this Memory pool.");
    DCHECK(!m_free.at(elemIndex),
           "Expected in-use ObjectBlock at index:" + std::to_string(elemIndex));

    if constexpr (!std::is_trivially_destructible_v<T>) {
      elem->~T();
    }

    m_free.at(elemIndex) = true;
  }

  auto updateNextFreeIndex() noexcept {
    const auto initial_free_index = m_nextFreeIndex;
    while (!m_free.at(m_nextFreeIndex)) {
      ++m_nextFreeIndex;
      if (m_nextFreeIndex == m_free.size()) [[unlikely]] {
        m_nextFreeIndex = 0;
      }
      ASSERT(initial_free_index != m_nextFreeIndex,
             "Memory Pool out of space.");
    }
  }

  NewMemPool() = delete;
  NewMemPool(const NewMemPool &) = delete;
  NewMemPool(NewMemPool &&) = delete;
  NewMemPool &operator=(const NewMemPool &) = delete;
  NewMemPool &operator=(NewMemPool &&) = delete;

private:
  std::vector<T> m_store;
  std::vector<bool> m_free;

  size_t m_nextFreeIndex = 0;
};

} // namespace new_common
