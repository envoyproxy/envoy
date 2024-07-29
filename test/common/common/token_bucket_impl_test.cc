#include <chrono>

#include "source/common/common/token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_time.h"

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

class AtomicTokenBucketImplTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

// Verifies TokenBucket initialization.
TEST_F(AtomicTokenBucketImplTest, Initialization) {
  AtomicTokenBucketImpl token_bucket{1, time_system_, -1.0};

  EXPECT_EQ(1, token_bucket.fillRate());
  EXPECT_EQ(1, token_bucket.maxTokens());
  EXPECT_EQ(1, token_bucket.remainingTokens());

  EXPECT_EQ(1, token_bucket.consume(1, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  EXPECT_EQ(false, token_bucket.consume());
}

// Verifies TokenBucket's maximum capacity.
TEST_F(AtomicTokenBucketImplTest, MaxBucketSize) {
  AtomicTokenBucketImpl token_bucket{3, time_system_, 1};

  EXPECT_EQ(1, token_bucket.fillRate());
  EXPECT_EQ(3, token_bucket.maxTokens());
  EXPECT_EQ(3, token_bucket.remainingTokens());

  EXPECT_EQ(3, token_bucket.consume(3, false));
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_EQ(0, token_bucket.consume(4, false));
  EXPECT_EQ(3, token_bucket.consume(3, false));
}

// Verifies that TokenBucket can consume tokens.
TEST_F(AtomicTokenBucketImplTest, Consume) {
  AtomicTokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_EQ(0, token_bucket.consume(20, false));
  EXPECT_EQ(9, token_bucket.consume(9, false));

  // consume() == consume(1, false)
  EXPECT_EQ(true, token_bucket.consume());

  time_system_.setMonotonicTime(std::chrono::milliseconds(999));
  EXPECT_EQ(0, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(5999));
  EXPECT_EQ(0, token_bucket.consume(6, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(6000));
  EXPECT_EQ(6, token_bucket.consume(6, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
}

// Verifies that TokenBucket can refill tokens.
TEST_F(AtomicTokenBucketImplTest, Refill) {
  AtomicTokenBucketImpl token_bucket{1, time_system_, 0.5};
  EXPECT_EQ(1, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(1500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(2000));
  EXPECT_EQ(1, token_bucket.consume(1, false));
}

// Test partial consumption of tokens.
TEST_F(AtomicTokenBucketImplTest, PartialConsumption) {
  AtomicTokenBucketImpl token_bucket{16, time_system_, 16};
  EXPECT_EQ(16, token_bucket.consume(18, true));
  time_system_.advanceTimeWait(std::chrono::milliseconds(62));
  EXPECT_EQ(0, token_bucket.consume(1, true));
  time_system_.advanceTimeWait(std::chrono::milliseconds(1));
  EXPECT_EQ(1, token_bucket.consume(2, true));
}

// Validate that a minimal refresh time is 1 year.
TEST_F(AtomicTokenBucketImplTest, YearlyMinRefillRate) {
  constexpr uint64_t seconds_per_year = 365 * 24 * 60 * 60;
  // Set the fill rate to be 2 years.
  AtomicTokenBucketImpl token_bucket{1, time_system_, 1.0 / (seconds_per_year * 2)};

  // Consume first token.
  EXPECT_EQ(1, token_bucket.consume(1, false));

  // Less than a year should still have no tokens.
  time_system_.setMonotonicTime(std::chrono::seconds(seconds_per_year - 1));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::seconds(seconds_per_year));
  EXPECT_EQ(1, token_bucket.consume(1, false));
}

TEST_F(AtomicTokenBucketImplTest, ConsumeNegativeTokens) {
  AtomicTokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_EQ(3, token_bucket.consume([](double) { return 3; }));
  EXPECT_EQ(7, token_bucket.remainingTokens());
  EXPECT_EQ(-3, token_bucket.consume([](double) { return -3; }));
  EXPECT_EQ(10, token_bucket.remainingTokens());
}

TEST_F(AtomicTokenBucketImplTest, ConsumeSuperLargeTokens) {
  AtomicTokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_EQ(100, token_bucket.consume([](double) { return 100; }));
  EXPECT_EQ(-90, token_bucket.remainingTokens());
}

TEST_F(AtomicTokenBucketImplTest, MultipleThreadsConsume) {
  // Real time source to ensure we will not fall into endless loop.
  Event::TestRealTimeSystem real_time_source;

  AtomicTokenBucketImpl token_bucket{1200, time_system_, 1.0};

  // Exhaust all tokens.
  EXPECT_EQ(1200, token_bucket.consume(1200, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));

  std::vector<std::thread> threads;
  auto timeout_point = real_time_source.monotonicTime() + std::chrono::seconds(30);

  size_t thread_1_token = 0;
  threads.push_back(std::thread([&] {
    while (thread_1_token < 300 && real_time_source.monotonicTime() < timeout_point) {
      thread_1_token += token_bucket.consume(1, false);
    }
  }));

  size_t thread_2_token = 0;
  threads.push_back(std::thread([&] {
    while (thread_2_token < 300 && real_time_source.monotonicTime() < timeout_point) {
      thread_2_token += token_bucket.consume(1, false);
    }
  }));

  size_t thread_3_token = 0;
  threads.push_back(std::thread([&] {
    while (thread_3_token < 300 && real_time_source.monotonicTime() < timeout_point) {
      const size_t left = 300 - thread_3_token;
      thread_3_token += token_bucket.consume(std::min<size_t>(left, 2), true);
    }
  }));

  size_t thread_4_token = 0;
  threads.push_back(std::thread([&] {
    while (thread_4_token < 300 && real_time_source.monotonicTime() < timeout_point) {
      const size_t left = 300 - thread_4_token;
      thread_4_token += token_bucket.consume(std::min<size_t>(left, 3), true);
    }
  }));

  // Fill the buckets by changing the time.
  for (size_t i = 0; i < 200; i++) {
    time_system_.advanceTimeWait(std::chrono::seconds(1));
  }
  for (size_t i = 0; i < 100; i++) {
    time_system_.advanceTimeWait(std::chrono::seconds(10));
  }

  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(1200, thread_1_token + thread_2_token + thread_3_token + thread_4_token);

  EXPECT_EQ(0, token_bucket.consume(1, false));
}

} // namespace Envoy
