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

// Verifies a freshly constructed queue reads empty.
TEST_F(LFQueueTest, NewQueueIsEmpty) {
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies reading an empty queue returns nullptr.
TEST_F(LFQueueTest, ReadOnEmptyReturnsNull) {
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies one write makes the queue readable.
TEST_F(LFQueueTest, SingleWriteMakesReadable) {
  auto *w = q.getNextToWriteTo();
  ASSERT_NE(w, nullptr);
  w->value = 42;
  q.updateWriteIndex();
  EXPECT_NE(q.getNextToRead(), nullptr);
}

// Verifies FIFO round-trip preserves the written value.
TEST_F(LFQueueTest, WriteThenReadReturnsSameValue) {
  auto *w = q.getNextToWriteTo();
  ASSERT_NE(w, nullptr);
  w->value = 7;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 7);
}

// Verifies consuming an element makes the queue empty again.
TEST_F(LFQueueTest, ReadThenEmpties) {
  auto *w = q.getNextToWriteTo();
  ASSERT_NE(w, nullptr);
  w->value = 1;
  q.updateWriteIndex();
  q.updateReadIndex();
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies elements are read back in FIFO order.
TEST_F(LFQueueTest, FifoOrderPreserved) {
  for (int i = 0; i < 4; ++i) {
    auto *w = q.getNextToWriteTo();
    ASSERT_NE(w, nullptr);
    w->value = i;
    q.updateWriteIndex();
  }
  for (int i = 0; i < 4; ++i) {
    ASSERT_NE(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.getNextToRead()->value, i);
    q.updateReadIndex();
  }
}

// Verifies a full queue returns nullptr for writes and drains fully.
// Usable capacity is numElems - 1 (one slot distinguishes full vs empty).
TEST_F(LFQueueTest, FillToCapacityThenDrain) {
  for (int i = 0; i < 7; ++i) {
    auto *w = q.getNextToWriteTo();
    ASSERT_NE(w, nullptr);
    w->value = i;
    q.updateWriteIndex();
  }
  EXPECT_EQ(q.getNextToWriteTo(), nullptr);
  for (int i = 0; i < 7; ++i) {
    ASSERT_NE(q.getNextToRead(), nullptr);
    EXPECT_EQ(q.getNextToRead()->value, i);
    q.updateReadIndex();
  }
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies ring-buffer wrap-around keeps FIFO order.
TEST_F(LFQueueTest, WrapAroundPreservesOrder) {
  for (int i = 0; i < 6; ++i) {
    auto *w = q.getNextToWriteTo();
    ASSERT_NE(w, nullptr);
    w->value = i;
    q.updateWriteIndex();
  }
  for (int i = 0; i < 6; ++i) {
    q.updateReadIndex();
  }
  for (int i = 100; i < 106; ++i) {
    auto *w = q.getNextToWriteTo();
    ASSERT_NE(w, nullptr);
    w->value = i;
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
  auto *w1 = q.getNextToWriteTo();
  ASSERT_NE(w1, nullptr);
  w1->value = 1;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 1);
  q.updateReadIndex();

  auto *w2 = q.getNextToWriteTo();
  ASSERT_NE(w2, nullptr);
  w2->value = 2;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 2);
  q.updateReadIndex();

  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies a slot can be reused after being consumed.
TEST_F(LFQueueTest, WriteSlotIsReusableAfterRead) {
  auto *w1 = q.getNextToWriteTo();
  ASSERT_NE(w1, nullptr);
  w1->value = 9;
  q.updateWriteIndex();
  q.updateReadIndex();

  auto *w2 = q.getNextToWriteTo();
  ASSERT_NE(w2, nullptr);
  w2->value = 10;
  q.updateWriteIndex();
  EXPECT_EQ(q.getNextToRead()->value, 10);
}

// Verifies nullptr tracks a mixed sequence of operations.
TEST_F(LFQueueTest, NullTracksMultipleOps) {
  EXPECT_EQ(q.getNextToRead(), nullptr);
  auto *w1 = q.getNextToWriteTo();
  ASSERT_NE(w1, nullptr);
  w1->value = 1;
  q.updateWriteIndex();
  EXPECT_NE(q.getNextToRead(), nullptr);
  auto *w2 = q.getNextToWriteTo();
  ASSERT_NE(w2, nullptr);
  w2->value = 2;
  q.updateWriteIndex();
  EXPECT_NE(q.getNextToRead(), nullptr);
  q.updateReadIndex();
  EXPECT_NE(q.getNextToRead(), nullptr);
  q.updateReadIndex();
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies a larger power-of-two queue constructs and operates.
TEST_F(LFQueueTest, LargePowerOfTwoQueueWorks) {
  LFQueue<TestElem> big(1024);
  EXPECT_EQ(big.getNextToRead(), nullptr);
  auto *w = big.getNextToWriteTo();
  ASSERT_NE(w, nullptr);
  w->value = 5;
  big.updateWriteIndex();
  EXPECT_EQ(big.getNextToRead()->value, 5);
}

// Verifies draining the last element makes the queue read-empty again.
TEST_F(LFQueueTest, DrainReturnsNullAgain) {
  auto *w = q.getNextToWriteTo();
  ASSERT_NE(w, nullptr);
  w->value = 1;
  q.updateWriteIndex();
  ASSERT_NE(q.getNextToRead(), nullptr);
  q.updateReadIndex();
  EXPECT_EQ(q.getNextToRead(), nullptr);
}

// Verifies non-power-of-two sizes are rejected (mask indexing requires PoT).
TEST(LFQueueDeathTest, NonPowerOfTwoSizeDies) {
  EXPECT_DEATH({ LFQueue<TestElem> bad(7); }, ".*");
}

// Verifies many wrap-around cycles keep FIFO order (exercises mask wrap).
TEST_F(LFQueueTest, ManyWrapCyclesPreserveOrder) {
  int next = 0;
  for (int cycle = 0; cycle < 10; ++cycle) {
    for (int i = 0; i < 7; ++i) {
      auto *w = q.getNextToWriteTo();
      ASSERT_NE(w, nullptr);
      w->value = next++;
      q.updateWriteIndex();
    }
    for (int i = 0; i < 7; ++i) {
      ASSERT_NE(q.getNextToRead(), nullptr);
      EXPECT_EQ(q.getNextToRead()->value, cycle * 7 + i);
      q.updateReadIndex();
    }
  }
  EXPECT_EQ(q.getNextToRead(), nullptr);
}
