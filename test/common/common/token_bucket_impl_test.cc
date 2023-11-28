#include <chrono>

#include "source/common/common/token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {

class TokenBucketImplTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

// Verifies TokenBucket initialization.
TEST_F(TokenBucketImplTest, Initialization) {
  TokenBucketImpl token_bucket{1, time_system_, -1.0};

  EXPECT_EQ(1, token_bucket.consume(1, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
}

// Verifies TokenBucket's maximum capacity.
TEST_F(TokenBucketImplTest, MaxBucketSize) {
  TokenBucketImpl token_bucket{3, time_system_, 1};

  EXPECT_EQ(3, token_bucket.consume(3, false));
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_EQ(0, token_bucket.consume(4, false));
  EXPECT_EQ(3, token_bucket.consume(3, false));
}

// Verifies that TokenBucket can consume tokens.
TEST_F(TokenBucketImplTest, Consume) {
  TokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_EQ(0, token_bucket.consume(20, false));
  EXPECT_EQ(9, token_bucket.consume(9, false));

  EXPECT_EQ(1, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(999));
  EXPECT_EQ(0, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(5999));
  EXPECT_EQ(0, token_bucket.consume(6, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(6000));
  EXPECT_EQ(6, token_bucket.consume(6, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
}

// Verifies that TokenBucket can refill tokens.
TEST_F(TokenBucketImplTest, Refill) {
  TokenBucketImpl token_bucket{1, time_system_, 0.5};
  EXPECT_EQ(1, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(1500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(2000));
  EXPECT_EQ(1, token_bucket.consume(1, false));
}

TEST_F(TokenBucketImplTest, NextTokenAvailable) {
  TokenBucketImpl token_bucket{10, time_system_, 5};
  EXPECT_EQ(9, token_bucket.consume(9, false));
  EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable());
  EXPECT_EQ(1, token_bucket.consume(1, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  EXPECT_EQ(std::chrono::milliseconds(200), token_bucket.nextTokenAvailable());
}

// Test partial consumption of tokens.
TEST_F(TokenBucketImplTest, PartialConsumption) {
  TokenBucketImpl token_bucket{16, time_system_, 16};
  EXPECT_EQ(16, token_bucket.consume(18, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
  time_system_.advanceTimeWait(std::chrono::milliseconds(62));
  EXPECT_EQ(0, token_bucket.consume(1, true));
  time_system_.advanceTimeWait(std::chrono::milliseconds(1));
  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
}

// Test reset functionality.
TEST_F(TokenBucketImplTest, Reset) {
  TokenBucketImpl token_bucket{16, time_system_, 16};
  token_bucket.maybeReset(1);
  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
}

// Verifies that TokenBucket can consume tokens and return next token time.
TEST_F(TokenBucketImplTest, ConsumeAndNextToken) {
  TokenBucketImpl token_bucket{10, time_system_, 5};

  // Exhaust all tokens.
  std::chrono::milliseconds time_to_next_token(0);
  EXPECT_EQ(10, token_bucket.consume(20, true, time_to_next_token));
  EXPECT_EQ(time_to_next_token.count(), 200);
  EXPECT_EQ(time_to_next_token, token_bucket.nextTokenAvailable());
  time_system_.advanceTimeWait(std::chrono::milliseconds(400));

  // Start a thread and call consume to refill tokens.
  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));
  EXPECT_EQ(time_to_next_token.count(), 0);
  EXPECT_EQ(time_to_next_token, token_bucket.nextTokenAvailable());
}

// Validate that a minimal refresh time is 1 year.
TEST_F(TokenBucketImplTest, YearlyMinRefillRate) {
  constexpr uint64_t seconds_per_year = 365 * 24 * 60 * 60;
  // Set the fill rate to be 2 years.
  TokenBucketImpl token_bucket{1, time_system_, 1.0 / (seconds_per_year * 2)};

  // Consume first token.
  EXPECT_EQ(1, token_bucket.consume(1, false));

  // Less than a year should still have no tokens.
  time_system_.setMonotonicTime(std::chrono::seconds(seconds_per_year - 1));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::seconds(seconds_per_year));
  EXPECT_EQ(1, token_bucket.consume(1, false));
}

} // namespace Envoy
