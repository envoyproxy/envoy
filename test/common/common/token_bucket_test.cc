#include <chrono>
#include <thread>

#include "common/common/token_bucket.h"

#include "gtest/gtest.h"

namespace Envoy {

namespace {
using half_token_per_sec = std::ratio<2L, 1L>;
using token_per_sec = std::ratio<1L, 1L>;
} // namespace

TEST(TokenBucketTest, ConsumeTokenPerSecond) {
  TokenBucket<token_per_sec> token_bucket{2};

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

TEST(TokenBucketTest, ConsumeTokenPerSecond) {
  TokenBucket<token_per_sec> token_bucket{2, ProdMonotonicTimeSource::instance_.currentTime() -
                                                 std::chrono::milliseconds(3000)};

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

TEST(TokenBucketTest, ConsumeHalfTokenSecond) {
  TokenBucket<half_token_per_sec> token_bucket{1};

  EXPECT_TRUE(token_bucket.consume());
  EXPECT_FALSE(token_bucket.consume());
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_FALSE(token_bucket.consume());
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  EXPECT_TRUE(token_bucket.consume());
}

} // namespace Envoy
