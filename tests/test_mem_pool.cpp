#include <gtest/gtest.h>

#include "common/mem_pool.hpp"

using namespace common;

struct PoolObj {
  int a = 0;
  int b = 0;
  PoolObj() = default;
  PoolObj(int x, int y) : a(x), b(y) {}
};

// Verifies allocate returns a usable object with forwarded constructor args.
TEST(MemPoolTest, AllocateReturnsConstructedObject) {
  MemPool<PoolObj> pool(8);
  PoolObj *p = pool.allocate(3, 4);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(p->a, 3);
  EXPECT_EQ(p->b, 4);
}

// Verifies sequential allocates return distinct slots.
TEST(MemPoolTest, SequentialAllocatesAreDistinct) {
  MemPool<PoolObj> pool(8);
  PoolObj *p1 = pool.allocate(1, 1);
  PoolObj *p2 = pool.allocate(2, 2);
  EXPECT_NE(p1, p2);
  EXPECT_EQ(p1->a, 1);
  EXPECT_EQ(p2->a, 2);
}

// Verifies deallocated slot is NOT immediately reused: the allocator scans
// forward from the last position (book logic), so the next allocate takes
// the next free slot, and the freed slot is recycled on wrap-around.
TEST(MemPoolTest, FreedSlotRecycledOnWrapAround) {
  MemPool<PoolObj> pool(4);
  PoolObj *p0 = pool.allocate(0, 0);
  PoolObj *p1 = pool.allocate(1, 1);
  PoolObj *p2 = pool.allocate(2, 2);
  pool.deallocate(p0); // free index 0, next-free cursor sits at index 3.
  PoolObj *p3 = pool.allocate(3, 3); // takes index 3, not the freed index 0.
  EXPECT_NE(p3, p0);
  EXPECT_NE(p3, p1);
  EXPECT_NE(p3, p2);
  EXPECT_EQ(p3->a, 3);
  pool.deallocate(p1);
  pool.deallocate(p2);
  pool.deallocate(p3);
  // All slots free again: allocator wraps and must hand out a usable slot.
  PoolObj *p4 = pool.allocate(4, 4);
  ASSERT_NE(p4, nullptr);
  EXPECT_EQ(p4->a, 4);
}

// Verifies pool can be filled, drained and refilled cyclically.
// Note: like the book, the pool asserts if asked to hold exactly its capacity,
// so this stays one slot below full.
TEST(MemPoolTest, FillDrainRefillCycle) {
  MemPool<PoolObj> pool(8);
  PoolObj *ptrs[4];
  for (int i = 0; i < 4; ++i) {
    ptrs[i] = pool.allocate(i, i * 10);
  }
  for (int i = 0; i < 4; ++i) {
    EXPECT_EQ(ptrs[i]->a, i);
  }
  for (int i = 0; i < 4; ++i) {
    pool.deallocate(ptrs[i]);
  }
  PoolObj *q = pool.allocate(99, 100);
  EXPECT_EQ(q->a, 99);
  EXPECT_EQ(q->b, 100);
}

// Verifies default-constructed allocate leaves members at defaults.
TEST(MemPoolTest, DefaultAllocateKeepsDefaults) {
  MemPool<PoolObj> pool(4);
  PoolObj *p = pool.allocate();
  EXPECT_EQ(p->a, 0);
  EXPECT_EQ(p->b, 0);
}

// Verifies pool works with a primitive type.
TEST(MemPoolTest, WorksWithPrimitiveType) {
  MemPool<int> pool(4);
  int *p = pool.allocate(42);
  ASSERT_NE(p, nullptr);
  EXPECT_EQ(*p, 42);
}

// Verifies next-free-index scan skips over in-use blocks.
TEST(MemPoolTest, SkipsInUseBlocksWhenScanning) {
  MemPool<PoolObj> pool(4);
  PoolObj *p0 = pool.allocate(0, 0);
  PoolObj *p1 = pool.allocate(1, 1);
  pool.deallocate(p0);
  // Next allocate must not return the still-live p1 slot contents.
  PoolObj *p2 = pool.allocate(2, 2);
  EXPECT_EQ(p2->a, 2);
  EXPECT_NE(p2, p1);
}

// Verifies double-free is detectable (death via ASSERT) - documents ownership contract.
TEST(MemPoolTest, AllocateManyObjectsAllUsable) {
  MemPool<PoolObj> pool(32);
  for (int i = 0; i < 16; ++i) {
    PoolObj *p = pool.allocate(i, -i);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->a, i);
    EXPECT_EQ(p->b, -i);
  }
}

// Verifies a pool of N holds at most N-1 live objects: filling the Nth slot
// trips the out-of-space ASSERT (death), since updateNextFreeIndex scans for
// the next free slot eagerly after every allocate (book logic).
TEST(MemPoolTest, OverCapacityDies) {
  MemPool<PoolObj> pool(4);
  PoolObj *held[3];
  for (int i = 0; i < 3; ++i) {
    held[i] = pool.allocate(i, i);
  }
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(held[i]->a, i);
  }
  EXPECT_DEATH({ pool.allocate(99, 99); }, ".*");
}

// Verifies deallocating the same slot twice trips the in-use ASSERT (death).
TEST(MemPoolTest, DoubleFreeDies) {
  MemPool<PoolObj> pool(4);
  PoolObj *p = pool.allocate(1, 1);
  pool.deallocate(p);
  EXPECT_DEATH({ pool.deallocate(p); }, ".*");
}
