
#include "ray/util/exponential_backoff.h"

#include <bits/stdc++.h>

#include "gtest/gtest.h"

namespace ray {

TEST(ExponentialBackoffTest, TestExponentialIncrease) {
  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(0, 157), 157 * 1);
  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(1, 157), 157 * 2);
  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(2, 157), 157 * 4);
  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(3, 157), 157 * 8);

  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(10, 0), 0);
  ASSERT_EQ(ExponentialBackoff::GetBackoffMs(11, 0), 0);
}

TEST(ExponentialBackoffTest, TestExceedMaxAttemptReturnsMaxAttempt) {
  auto backoff = ExponentialBackoff::GetBackoffMs(
      /*attempt*/ 11,
      /*base_ms*/ 1,
      /*max_attempt*/ 5,
      /*max_backoff_ms*/ std::numeric_limits<uint64_t>::max());
  ASSERT_EQ(backoff, static_cast<uint64_t>(pow(2, 5)));
}

TEST(ExponentialBackoffTest, TestExceedMaxBackoffReturnsMaxBackoff) {
  auto backoff = ExponentialBackoff::GetBackoffMs(
      /*attempt*/ 10, /*base_ms*/ 1, /*max_attempt*/ 10, /*max_backoff_ms*/ 5);
  ASSERT_EQ(backoff, 5);
}

TEST(ExponentialBackoffTest, TestOverflowReturnsMaxAttempt) {
  // 2 ^ 80 will overflow.
  auto backoff = ExponentialBackoff::GetBackoffMs(
      /*attempt*/ 80,
      /*base_ms*/ 1,
      /*max_attempt*/ 50,
      /*max_backoff_ms*/ std::numeric_limits<uint64_t>::max());
  ASSERT_EQ(backoff, static_cast<uint64_t>(pow(2, 50)));
}

TEST(ExponentialBackoffTest, TestOverflow) {
  // 2 ^ 80 will overflow.
  auto backoff = ExponentialBackoff::GetBackoffMs(
      /*attempt*/ 80,
      /*base_ms*/ 1,
      /*max_attempt*/ 80,
      /*max_backoff_ms*/ std::numeric_limits<uint64_t>::max());
  ASSERT_EQ(backoff, static_cast<uint64_t>(pow(2, 50)));
}

}  // namespace ray

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
