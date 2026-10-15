// Local copy of the free-list based MemPool for the isolated mem_pool
// benchmark. Renamed to new_common::NewMemPool so it can be compared against
// old_common::OldMemPool in the same translation unit. Shared infra
// (ASSERT et al.) is still taken from include/common.
#pragma once

#include <list>
#include <string>
#include <vector>

#include "common/macros.hpp"

namespace new_common {

template <typename T> class NewMemPool final {
public:
  explicit NewMemPool(std::size_t numElems) : m_store(numElems, {T(), true}) {
    ASSERT(reinterpret_cast<const ObjectBlock *>(&(m_store.at(0).object_)) ==
               &(m_store.at(0)),
           "T object should be first member of ObjectBlock.");
  }

  template <typename... Args> T *allocate(Args... args) noexcept {
    auto ind = m_nextFreeIndex;
    if (!m_freeIndices.empty()) {
      ind = m_freeIndices.front();
      m_freeIndices.pop_front();
    } else {
      ++m_nextFreeIndex;
    }

    ASSERT(ind < m_store.size(),
           "Memory Pool out of space. Requested index:" + std::to_string(ind) +
               " max size:" + std::to_string(m_store.size()));

    auto obj_block = &(m_store.at(ind));
    ASSERT(obj_block->is_free_, "Expected free ObjectBlock at index:" +
                                    std::to_string(m_nextFreeIndex));

    T *ret = &(obj_block->object_);
    ret = new (ret) T(args...);
    obj_block->is_free_ = false;

    return ret;
  }

  auto deallocate(const T *elem) noexcept {
    const auto elemIndex =
        (reinterpret_cast<const ObjectBlock *>(elem) - &m_store.at(0));
    ASSERT(elemIndex >= 0 && static_cast<size_t>(elemIndex) < m_store.size(),
           "Element being deallocated does not belong to this Memory pool.");
    ASSERT(!m_store.at(elemIndex).is_free_,
           "Expected in-use ObjectBlock at index:" + std::to_string(elemIndex));
    m_store.at(elemIndex).is_free_ = true;
    m_freeIndices.push_back(elemIndex);
  }

  NewMemPool() = delete;
  NewMemPool(const NewMemPool &) = delete;
  NewMemPool(NewMemPool &&) = delete;
  NewMemPool &operator=(const NewMemPool &) = delete;
  NewMemPool &operator=(NewMemPool &&) = delete;

private:
  struct ObjectBlock {
    T object_;
    bool is_free_ = true;
  };

  std::vector<ObjectBlock> m_store;
  std::list<size_t> m_freeIndices;

  size_t m_nextFreeIndex = 0;
};

} // namespace new_common
