#pragma once

#include <atomic>
#include <bit>
#include <string>
#include <vector>

#include "macros.hpp"

namespace common {

// Single produce single consumer safe queue
template <typename T> class LFQueue final {
public:
  explicit LFQueue(std::size_t numElems) : m_store(numElems, T()) {
    ASSERT(numElems > 2 && std::popcount(numElems) == 1,
           "LFQueue size must be a power of 2 for efficient modulo operation.");
    m_mask = numElems - 1;
  }

  T *getNextToWriteTo() noexcept {
    return &m_store[m_nextWriteIndex];
  }

  void updateWriteIndex() noexcept {
    m_nextWriteIndex = (m_nextWriteIndex + 1) & m_mask;
    m_numElements++;
  }

  const T *getNextToRead() const noexcept {
    return (size() ? &m_store[m_nextReadIndex] : nullptr);
  }

  void updateReadIndex() noexcept {
    m_nextReadIndex = (m_nextReadIndex + 1) & m_mask;
    ASSERT(m_numElements != 0,
           "Read an invalid element in:" + std::to_string(pthread_self()));
    m_numElements--;
  }

  size_t size() const noexcept {
    return m_numElements.load();
  }

  LFQueue() = delete;
  LFQueue(const LFQueue &) = delete;
  LFQueue(const LFQueue &&) = delete;
  LFQueue &operator=(const LFQueue &) = delete;
  LFQueue &operator=(const LFQueue &&) = delete;

private:
  std::vector<T> m_store;
  size_t m_mask = 0;

  std::atomic<size_t> m_nextWriteIndex = {0};
  std::atomic<size_t> m_nextReadIndex = {0};

  std::atomic<size_t> m_numElements = {0};
};

} // namespace common
