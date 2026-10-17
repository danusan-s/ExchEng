#include <cstdlib>
#include <iostream>
#include <vector>

#include "common/perf_utils.hpp"
#include "new_mem_pool.hpp"
#include "old_mem_pool.hpp"

struct BenchObj {
  int a = 0;
  int b = 0;
  char padding[56] = {};
  BenchObj() = default;
  BenchObj(int x, int y) : a(x), b(y) {}
};

template <typename PoolT> size_t benchmarkSingleSlotReuse() {
  constexpr size_t pool_size = 1024;
  constexpr size_t loop_count = 200000;
  PoolT pool(pool_size);
  size_t total_rdtsc = 0;
  volatile int sink = 0;
  for (size_t i = 0; i < loop_count; ++i) {
    const auto start = common::rdtsc();
    auto *p = pool.allocate(static_cast<int>(i), static_cast<int>(i + 1));
    sink += p->a;
    pool.deallocate(p);
    total_rdtsc += (common::rdtsc() - start);
  }
  (void)sink;
  return (total_rdtsc / loop_count);
}

template <typename PoolT> size_t benchmarkFragmented() {
  constexpr size_t pool_size = 1024;
  constexpr size_t live_count = 512;
  constexpr size_t loop_count = 200000;
  PoolT pool(pool_size);

  // Fragment the pool: hold live_count objects, then free every other slot
  // so the next allocate must skip over in-use blocks (scan) vs. pop a
  // free-list entry.
  std::vector<decltype(pool.allocate())> holders;
  holders.reserve(live_count);
  for (size_t i = 0; i < live_count; ++i) {
    holders.push_back(pool.allocate(static_cast<int>(i), static_cast<int>(i)));
  }
  for (size_t i = 0; i < live_count; i += 2) {
    pool.deallocate(holders[i]);
    holders[i] = nullptr;
  }

  size_t total_rdtsc = 0;
  volatile int sink = 0;
  for (size_t i = 0; i < loop_count; ++i) {
    const auto start = common::rdtsc();
    auto *p = pool.allocate(static_cast<int>(i), static_cast<int>(i + 1));
    sink += p->a;
    pool.deallocate(p);
    total_rdtsc += (common::rdtsc() - start);
  }
  (void)sink;

  for (auto *p : holders) {
    if (p) {
      pool.deallocate(p);
    }
  }
  return (total_rdtsc / loop_count);
}

int main(int, char **) {
  {
    const auto cycles =
        benchmarkSingleSlotReuse<new_common::NewMemPool<BenchObj>>();
    std::cout << "NEW MEMPOOL (single-slot reuse) " << cycles
              << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  {
    const auto cycles =
        benchmarkSingleSlotReuse<old_common::OldMemPool<BenchObj>>();
    std::cout << "ORIGINAL MEMPOOL (single-slot reuse) " << cycles
              << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  {
    const auto cycles =
        benchmarkFragmented<new_common::NewMemPool<BenchObj>>();
    std::cout << "NEW MEMPOOL (fragmented) " << cycles
              << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  {
    const auto cycles =
        benchmarkFragmented<old_common::OldMemPool<BenchObj>>();
    std::cout << "ORIGINAL MEMPOOL (fragmented) " << cycles
              << " CLOCK CYCLES PER OPERATION." << std::endl;
  }

  exit(EXIT_SUCCESS);
}
