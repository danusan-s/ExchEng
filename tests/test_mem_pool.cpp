#include <gtest/gtest.h>

#include "common/mem_pool.hpp"

using namespace common;

struct PoolObj {
  int a = 0;
  int b = 0;
  PoolObj() = default;
  PoolObj(int x, int y) : a(x), b(y) {
  }
};

static int g_destructed = 0;

struct Tracked {
  int v = 0;
  Tracked() = default;
  explicit Tracked(int x) : v(x) {
  }
  ~Tracked() {
    ++g_destructed;
  }
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

// Verifies a freed slot is handed straight back on the very next allocate.
// The new pool keeps a free-list stack: deallocate pushes the index and
// allocate pops it, so the freed slot is reused immediately (no scan).
TEST(MemPoolTest, FreedSlotImmediatelyReused) {
  MemPool<PoolObj> pool(4);
  PoolObj *p0 = pool.allocate(0, 0);
  PoolObj *p1 = pool.allocate(1, 1);
  pool.deallocate(p0);
  PoolObj *p2 = pool.allocate(2, 2);
  EXPECT_EQ(p2, p0); // recycled right away, not on wrap-around.
  EXPECT_EQ(p2->a, 2);
  EXPECT_NE(p2, p1);
}

// Verifies the free list is LIFO: the most recently freed slot comes back
// first on the following allocate.
TEST(MemPoolTest, FreeListIsLifo) {
  MemPool<PoolObj> pool(4);
  PoolObj *p0 = pool.allocate(0, 0);
  PoolObj *p1 = pool.allocate(1, 1);
  pool.deallocate(p0);
  pool.deallocate(p1); // freed last, so reused first.
  EXPECT_EQ(pool.allocate(), p1);
  EXPECT_EQ(pool.allocate(), p0);
}

// Verifies pool can be filled, drained and refilled cyclically.
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

// Verifies live slots are never handed out twice.
TEST(MemPoolTest, LiveSlotsAreNotAliased) {
  MemPool<PoolObj> pool(8);
  PoolObj *held[6];
  for (int i = 0; i < 6; ++i) {
    held[i] = pool.allocate(i, i);
  }
  for (int i = 0; i < 6; ++i) {
    for (int j = i + 1; j < 6; ++j) {
      EXPECT_NE(held[i], held[j]);
    }
  }
}

// Verifies many allocated objects remain usable.
TEST(MemPoolTest, AllocateManyObjectsAllUsable) {
  MemPool<PoolObj> pool(32);
  for (int i = 0; i < 16; ++i) {
    PoolObj *p = pool.allocate(i, -i);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->a, i);
    EXPECT_EQ(p->b, -i);
  }
}

// Verifies a pool of N holds exactly N live objects; the (N+1)th allocate
// trips the out-of-space DCHECK (ASSERT in debug builds). Unlike the old
// scan-based pool, there is no lost slot.
TEST(MemPoolTest, ExactCapacityThenDies) {
  MemPool<PoolObj> pool(4);
  PoolObj *held[4];
  for (int i = 0; i < 4; ++i) {
    held[i] = pool.allocate(i, i);
    ASSERT_NE(held[i], nullptr);
    EXPECT_EQ(held[i]->a, i);
  }
  EXPECT_DEATH({ pool.allocate(99, 99); }, ".*");
}

// Documents that the pool does NOT detect double-free: deallocating an
// already-free slot pushes a duplicate index, so the slot is handed out
// twice. Callers must not double-free (unlike the old is_free_ pool).
TEST(MemPoolTest, DoubleFreeIsNotDetectedAndAliases) {
  MemPool<PoolObj> pool(4);
  PoolObj *p = pool.allocate(1, 1);
  pool.deallocate(p);
  pool.deallocate(p); // duplicate index pushed onto the free list.
  PoolObj *a = pool.allocate(2, 2);
  PoolObj *b = pool.allocate(3, 3);
  EXPECT_EQ(a, p);
  EXPECT_EQ(b, p);
}
