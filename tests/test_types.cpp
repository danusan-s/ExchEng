#include <gtest/gtest.h>

#include "common/types.hpp"

using namespace common;

// NOTE: intentional divergence from the book. The book uses
// Side{INVALID=0,BUY=1,SELL=-1} with sideToIndex=(size_t)side+1
// (SELL->0, INVALID->1, BUY->2 via unsigned wrap). This codebase uses
// Side{BUY=0,SELL=1,INVALID=2} with sideToIndex=(size_t)side so indices
// are direct and safe. These tests pin the local contract: distinct,
// in-bounds indices usable for per-side arrays.

// Verifies BUY/SELL/INVALID map to distinct indices usable as array slots.
TEST(TypesTest, SidesMapToDistinctIndices) {
  EXPECT_NE(sideToIndex(Side::BUY), sideToIndex(Side::SELL));
  EXPECT_NE(sideToIndex(Side::BUY), sideToIndex(Side::INVALID));
  EXPECT_NE(sideToIndex(Side::SELL), sideToIndex(Side::INVALID));
}

// Verifies BUY maps to index 0 for array lookups.
TEST(TypesTest, BuySideMapsToIndexZero) {
  EXPECT_EQ(sideToIndex(Side::BUY), 0u);
}

// Verifies SELL maps to index 1 for array lookups.
TEST(TypesTest, SellSideMapsToIndexOne) {
  EXPECT_EQ(sideToIndex(Side::SELL), 1u);
}

// Verifies indices stay in bounds for a 3-slot side array (BUY/SELL/INVALID).
TEST(TypesTest, SideIndicesAreInBoundsForSideArray) {
  EXPECT_LT(sideToIndex(Side::BUY), 3u);
  EXPECT_LT(sideToIndex(Side::SELL), 3u);
  EXPECT_LT(sideToIndex(Side::INVALID), 3u);
}

// Verifies BUY contributes +1 to signed position math.
TEST(TypesTest, BuySideValueIsPositiveOne) {
  EXPECT_EQ(sideToValue(Side::BUY), 1);
}

// Verifies SELL contributes -1 to signed position math.
TEST(TypesTest, SellSideValueIsNegativeOne) {
  EXPECT_EQ(sideToValue(Side::SELL), -1);
}

// Verifies INVALID side contributes 0 to position math.
TEST(TypesTest, InvalidSideValueIsZero) {
  EXPECT_EQ(sideToValue(Side::INVALID), 0);
}

// Verifies sideToString renders BUY correctly.
TEST(TypesTest, SideToStringBuy) {
  EXPECT_EQ(sideToString(Side::BUY), "BUY");
}

// Verifies sideToString renders SELL correctly.
TEST(TypesTest, SideToStringSell) {
  EXPECT_EQ(sideToString(Side::SELL), "SELL");
}

// Verifies sideToString renders INVALID distinctly (book returns "INVALID",
// this codebase returns "INVALID_SIDE" for grep-ability).
TEST(TypesTest, SideToStringInvalidIsDistinct) {
  const auto s = sideToString(Side::INVALID);
  EXPECT_NE(s, "BUY");
  EXPECT_NE(s, "SELL");
}

// Verifies orderIdToString renders the invalid sentinel symbolically.
TEST(TypesTest, InvalidOrderIdRendersSymbolically) {
  EXPECT_EQ(orderIdToString(INVALID_ORDER_ID), "INVALID_ORDER_ID");
}

// Verifies orderIdToString renders a normal id numerically.
TEST(TypesTest, ValidOrderIdRendersNumerically) {
  EXPECT_EQ(orderIdToString(42), "42");
}

// Verifies priceToString renders the invalid sentinel symbolically.
TEST(TypesTest, InvalidPriceRendersSymbolically) {
  EXPECT_EQ(priceToString(INVALID_PRICE), "INVALID_PRICE");
}

// Verifies priceToString renders a normal price numerically.
TEST(TypesTest, ValidPriceRendersNumerically) {
  EXPECT_EQ(priceToString(100), "100");
}

// Verifies quantityToString renders the invalid sentinel symbolically.
TEST(TypesTest, InvalidQuantityRendersSymbolically) {
  EXPECT_EQ(quantityToString(INVALID_QUANTITY), "INVALID_QUANTITY");
}

// Verifies quantityToString renders a normal quantity numerically.
TEST(TypesTest, ValidQuantityRendersNumerically) {
  EXPECT_EQ(quantityToString(100), "100");
}

// Verifies tickerIdToString renders the invalid sentinel symbolically.
TEST(TypesTest, InvalidTickerRendersSymbolically) {
  EXPECT_EQ(tickerIdToString(INVALID_TICKER_ID), "INVALID_TICKER_ID");
}

// Verifies clientIdToString renders a normal id numerically.
TEST(TypesTest, ValidClientIdRendersNumerically) {
  EXPECT_EQ(clientIdToString(7), "7");
}

// Verifies priorityToString renders the invalid sentinel symbolically.
TEST(TypesTest, InvalidPriorityRendersSymbolically) {
  EXPECT_EQ(priorityToString(INVALID_PRIORITY), "INVALID_PRIORITY");
}

// Verifies priorityToString renders a normal priority numerically.
TEST(TypesTest, ValidPriorityRendersNumerically) {
  EXPECT_EQ(priorityToString(3), "3");
}
