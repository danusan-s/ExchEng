#include <gtest/gtest.h>

#include "common/lf_queue.hpp"

using namespace common;

// Helper element type for queue tests.
struct TestElem {
  int value = 0;
};

// Test fixture with a small power-of-two queue.
class LFQueueTest : public ::testing::Test {
protected:
  LFQueue<TestElem> q{8};
};

// Verifies a freshly constructed queue reports size zero.
TEST_F(LFQueueTest, NewQueueIsEmpty) {
  EXPECT_EQ(q.size(), 0u);
}

// Verifies reading an empty queue returns nullptr.
TEST_F(LFQueueTest, ReadOnEmptyReturnsNull) {
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies one write increments size to one.
TEST_F(LFQueueTest, SingleWriteIncreasesSize) {
  auto *w = q.getNextToWriteTo();
  w->value = 42;
  q.updateWriteIndex();
  EXPECT_EQ(q.size(), 1u);
}

// Verifies FIFO round-trip preserves the written value.
TEST_F(LFQueueTest, WriteThenReadReturnsSameValue) {
  auto *w = q.getNextToWriteTo();
  w->value = 7;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 7);
}

// Verifies consuming an element decrements size back to zero.
TEST_F(LFQueueTest, ReadThenSizeDecreases) {
  auto *w = q.getNextToWriteTo();
  w->value = 1;
  q.updateWriteIndex();
  q.updateReadIndex();
  EXPECT_EQ(q.size(), 0u);
}

// Verifies elements are read back in FIFO order.
TEST_F(LFQueueTest, FifoOrderPreserved) {
  for (int i = 0; i < 4; ++i) {
    q.getNextToWriteTo()->value = i;
    q.updateWriteIndex();
  }
  for (int i = 0; i < 4; ++i) {
    ASSERT_NE(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.getNextToRead()->value, i);
    q.updateReadIndex();
  }
}

// Verifies queue can fill to capacity and drain fully.
TEST_F(LFQueueTest, FillToCapacityThenDrain) {
  for (int i = 0; i < 8; ++i) {
    q.getNextToWriteTo()->value = i;
    q.updateWriteIndex();
  }
  EXPECT_EQ(q.size(), 8u);
  for (int i = 0; i < 8; ++i) {
    ASSERT_NE(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.getNextToRead()->value, i);
    q.updateReadIndex();
  }
  EXPECT_EQ(q.size(), 0u);
}

// Verifies ring-buffer wrap-around keeps FIFO order.
TEST_F(LFQueueTest, WrapAroundPreservesOrder) {
  for (int i = 0; i < 6; ++i) {
    q.getNextToWriteTo()->value = i;
    q.updateWriteIndex();
  }
  for (int i = 0; i < 6; ++i) {
    q.updateReadIndex();
  }
  for (int i = 100; i < 106; ++i) {
    q.getNextToWriteTo()->value = i;
    q.updateWriteIndex();
  }
  for (int i = 100; i < 106; ++i) {
    ASSERT_NE(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.getNextToRead()->value, i);
    q.updateReadIndex();
  }
}

// Verifies interleaved single writes and reads work correctly.
TEST_F(LFQueueTest, InterleavedWriteRead) {
  q.getNextToWriteTo()->value = 1;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 1);
  q.updateReadIndex();

  q.getNextToWriteTo()->value = 2;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 2);
  q.updateReadIndex();

  EXPECT_EQ(q.size(), 0u);
}

// Verifies a slot can be reused after being consumed.
TEST_F(LFQueueTest, WriteSlotIsReusableAfterRead) {
  q.getNextToWriteTo()->value = 9;
  q.updateWriteIndex();
  q.updateReadIndex();

  q.getNextToWriteTo()->value = 10;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 10);
}

// Verifies size tracks a mixed sequence of operations.
TEST_F(LFQueueTest, SizeTracksMultipleOps) {
  EXPECT_EQ(q.size(), 0u);
  q.getNextToWriteTo()->value = 1;
  q.updateWriteIndex();
  EXPECT_EQ(q.size(), 1u);
  q.getNextToWriteTo()->value = 2;
  q.updateWriteIndex();
  EXPECT_EQ(q.size(), 2u);
  q.updateReadIndex();
  EXPECT_EQ(q.size(), 1u);
}

// Verifies a larger power-of-two queue constructs and operates.
TEST_F(LFQueueTest, LargePowerOfTwoQueueWorks) {
  LFQueue<TestElem> big(1024);
  EXPECT_EQ(big.size(), 0u);
  big.getNextToWriteTo()->value = 5;
  big.updateWriteIndex();
  EXPECT_EQ(big.getNextToRead()->value, 5);
}

// Verifies draining the last element makes the queue read-empty again.
TEST_F(LFQueueTest, DrainReturnsNullAgain) {
  q.getNextToWriteTo()->value = 1;
  q.updateWriteIndex();
  ASSERT_NE(q.getNextToRead(), nullptr);
  q.updateReadIndex();
  EXPECT_EQ(q.getNextToRead(), nullptr);
  EXPECT_EQ(q.size(), 0u);
}

// Verifies non-power-of-two sizes are rejected (mask indexing requires PoT).
TEST(LFQueueDeathTest, NonPowerOfTwoSizeDies) {
  EXPECT_DEATH({ LFQueue<TestElem> bad(7); }, ".*");
}

// Verifies many wrap-around cycles keep FIFO order (exercises mask wrap).
TEST_F(LFQueueTest, ManyWrapCyclesPreserveOrder) {
  int next = 0;
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (int i = 0; i < 8; ++i) {
      q.getNextToWriteTo()->value = next++;
      q.updateWriteIndex();
    }
    for (int i = 0; i < 8; ++i) {
      ASSERT_NE(q.getNextToRead(), nullptr);
      EXPECT_EQ(q.getNextToRead()->value, cycle * 8 + i);
      q.updateReadIndex();
    }
  }
  EXPECT_EQ(q.size(), 0u);
}
