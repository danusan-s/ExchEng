#pragma once

#include <string>
#include <vector>

#include "macros.hpp"

namespace common {

template <typename T> class MemPool final {
public:
  explicit MemPool(std::size_t numElems) : m_store(numElems, {T(), true}) {
    ASSERT(reinterpret_cast<const ObjectBlock *>(&(m_store[0].object_)) ==
               &(m_store[0]),
           "T object should be first member of ObjectBlock.");
  }

  template <typename... Args> T *allocate(Args... args) noexcept {
    auto obj_block = &(m_store[m_nextFreeIndex]);
    ASSERT(obj_block->is_free_, "Expected free ObjectBlock at index:" +
                                    std::to_string(m_nextFreeIndex));
    T *ret = &(obj_block->object_);
    ret = new (ret) T(args...);
    obj_block->is_free_ = false;

    updateNextFreeIndex();

    return ret;
  }

  auto deallocate(const T *elem) noexcept {
    const auto elemIndex =
        (reinterpret_cast<const ObjectBlock *>(elem) - &m_store[0]);
    ASSERT(elemIndex >= 0 && static_cast<size_t>(elemIndex) < m_store.size(),
           "Element being deallocated does not belong to this Memory pool.");
    ASSERT(!m_store[elemIndex].is_free_,
           "Expected in-use ObjectBlock at index:" + std::to_string(elemIndex));
    m_store[elemIndex].is_free_ = true;
  }

  auto updateNextFreeIndex() noexcept {
    const auto initial_free_index = m_nextFreeIndex;
    while (!m_store[m_nextFreeIndex].is_free_) {
      ++m_nextFreeIndex;
      if (m_nextFreeIndex == m_store.size()) [[unlikely]] {
        m_nextFreeIndex = 0;
      }
      if (initial_free_index == m_nextFreeIndex) [[unlikely]] {
        ASSERT(initial_free_index != m_nextFreeIndex,
               "Memory Pool out of space.");
      }
    }
  }

  MemPool() = delete;
  MemPool(const MemPool &) = delete;
  MemPool(const MemPool &&) = delete;
  MemPool &operator=(const MemPool &) = delete;
  MemPool &operator=(const MemPool &&) = delete;

private:
  struct ObjectBlock {
    T object_;
    bool is_free_ = true;
  };

  std::vector<ObjectBlock> m_store;

  size_t m_nextFreeIndex = 0;
};

} // namespace common
