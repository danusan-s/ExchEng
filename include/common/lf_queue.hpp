#pragma once

#include <atomic>
#include <bit>
#include <string>
#include <vector>

#include "common/macros.hpp"

namespace common {

// Single produce single consumer safe queue
template <typename T> class LFQueue final {
public:
  // Actual storable elements are numElems - 1, as one element is always
  // reserved to differentiate between full and empty states.
  explicit LFQueue(std::size_t numElems) : m_store(numElems, T()) {
    ASSERT(numElems > 2 && std::popcount(numElems) == 1,
           "LFQueue size must be a power of 2 for efficient modulo operation.");
    m_mask = numElems - 1;
  }

  T *getNextToWriteTo() noexcept {
    const auto writeIndex = m_nextWriteIndex.load(std::memory_order_relaxed);
    const auto nextReadIndex = m_nextReadIndex.load(std::memory_order_acquire);

    if (((writeIndex + 1) & m_mask) == nextReadIndex) [[unlikely]] {
      return nullptr; // Queue is full, cannot write to it.
    }

    return &m_store.at(writeIndex);
  }

  void updateWriteIndex() noexcept {
    const auto writeIndex = m_nextWriteIndex.load(std::memory_order_relaxed);
    m_nextWriteIndex.store((writeIndex + 1) & m_mask,
                           std::memory_order_release);
  }

  const T *getNextToRead() const noexcept {
    const auto readIndex = m_nextReadIndex.load(std::memory_order_relaxed);
    const auto nextWriteIndex =
        m_nextWriteIndex.load(std::memory_order_acquire);

    if (readIndex == nextWriteIndex) [[unlikely]] {
      return nullptr; // Queue is empty, cannot read from it.
    }

    return &m_store.at(readIndex);
  }

  void updateReadIndex() noexcept {
    const auto readIndex = m_nextReadIndex.load(std::memory_order_relaxed);
    m_nextReadIndex.store((readIndex + 1) & m_mask, std::memory_order_release);
  }

  LFQueue() = delete;
  LFQueue(const LFQueue &) = delete;
  LFQueue(LFQueue &&) = delete;
  LFQueue &operator=(const LFQueue &) = delete;
  LFQueue &operator=(LFQueue &&) = delete;

private:
  std::vector<T> m_store;
  size_t m_mask = 0;

  alignas(64) std::atomic<size_t> m_nextWriteIndex = {0};
  alignas(64) std::atomic<size_t> m_nextReadIndex = {0};
};

} // namespace common
