#include <chrono>
#include <thread>

#include "common/common/token_bucket.h"

#include "gtest/gtest.h"

namespace Envoy {

TEST(TokenBucketTest, ConsumeToken) {
  TokenBucket token_bucket{2};

  EXPECT_TRUE(token_bucket.consume());
  EXPECT_TRUE(token_bucket.consume());
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_FALSE(token_bucket.consume(2));
  std::this_thread::sleep_for(std::chrono::milliseconds(400));
  EXPECT_FALSE(token_bucket.consume(2));
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  EXPECT_TRUE(token_bucket.consume(2));
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  EXPECT_FALSE(token_bucket.consume());
}

TEST(TokenBucketTest, ConsumeTokenHalfSecond) {
  TokenBucket token_bucket{1, 0.5};

  EXPECT_TRUE(token_bucket.consume());
  EXPECT_FALSE(token_bucket.consume());
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_FALSE(token_bucket.consume());
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_TRUE(token_bucket.consume());
}

} // namespace Envoy
