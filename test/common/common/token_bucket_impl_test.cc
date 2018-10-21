#include <chrono>

#include "common/common/token_bucket_impl.h"

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

  EXPECT_TRUE(token_bucket.consume());
  EXPECT_FALSE(token_bucket.consume());
}

// Verifies TokenBucket's maximum capacity.
TEST_F(TokenBucketImplTest, MaxBucketSize) {
  TokenBucketImpl token_bucket{3, time_system_, 1};

  EXPECT_TRUE(token_bucket.consume(3));
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_FALSE(token_bucket.consume(4));
  EXPECT_TRUE(token_bucket.consume(3));
}

// Verifies that TokenBucket can consume tokens.
TEST_F(TokenBucketImplTest, Consume) {
  TokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_FALSE(token_bucket.consume(20));
  EXPECT_TRUE(token_bucket.consume(9));

  EXPECT_TRUE(token_bucket.consume());

  time_system_.setMonotonicTime(std::chrono::milliseconds(999));
  EXPECT_FALSE(token_bucket.consume());

  time_system_.setMonotonicTime(std::chrono::milliseconds(5999));
  EXPECT_FALSE(token_bucket.consume(6));

  time_system_.setMonotonicTime(std::chrono::milliseconds(6000));
  EXPECT_TRUE(token_bucket.consume(6));
  EXPECT_FALSE(token_bucket.consume());
}

// Verifies that TokenBucket can refill tokens.
TEST_F(TokenBucketImplTest, Refill) {
  TokenBucketImpl token_bucket{1, time_system_, 0.5};
  EXPECT_TRUE(token_bucket.consume());

  time_system_.setMonotonicTime(std::chrono::milliseconds(500));
  EXPECT_FALSE(token_bucket.consume());
  time_system_.setMonotonicTime(std::chrono::milliseconds(1500));
  EXPECT_FALSE(token_bucket.consume());
  time_system_.setMonotonicTime(std::chrono::milliseconds(2000));
  EXPECT_TRUE(token_bucket.consume());
}

} // namespace Envoy
