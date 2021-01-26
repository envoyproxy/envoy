#include <chrono>

#include "common/common/shared_token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {

class SharedTokenBucketImplTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;
};

// Verifies TokenBucket initialization.
TEST_F(SharedTokenBucketImplTest, Initialization) {
  SharedTokenBucketImpl token_bucket{1, time_system_, -1.0};

  EXPECT_EQ(1, token_bucket.consume(1, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
}

// Verifies TokenBucket's maximum capacity.
TEST_F(SharedTokenBucketImplTest, MaxBucketSize) {
  SharedTokenBucketImpl token_bucket{3, time_system_, 1};

  EXPECT_EQ(3, token_bucket.consume(3, false));
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_EQ(0, token_bucket.consume(4, false));
  EXPECT_EQ(3, token_bucket.consume(3, false));
}

// Verifies that TokenBucket can consume tokens.
TEST_F(SharedTokenBucketImplTest, Consume) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 1};

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
TEST_F(SharedTokenBucketImplTest, Refill) {
  SharedTokenBucketImpl token_bucket{1, time_system_, 0.5};
  EXPECT_EQ(1, token_bucket.consume(1, false));

  time_system_.setMonotonicTime(std::chrono::milliseconds(500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(1500));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  time_system_.setMonotonicTime(std::chrono::milliseconds(2000));
  EXPECT_EQ(1, token_bucket.consume(1, false));
}

TEST_F(SharedTokenBucketImplTest, NextTokenAvailable) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 5};
  EXPECT_EQ(9, token_bucket.consume(9, false));
  EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable());
  EXPECT_EQ(1, token_bucket.consume(1, false));
  EXPECT_EQ(0, token_bucket.consume(1, false));
  EXPECT_EQ(std::chrono::milliseconds(200), token_bucket.nextTokenAvailable());
}

// Test partial consumption of tokens.
TEST_F(SharedTokenBucketImplTest, PartialConsumption) {
  SharedTokenBucketImpl token_bucket{16, time_system_, 16};
  EXPECT_EQ(16, token_bucket.consume(18, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
  time_system_.advanceTimeWait(std::chrono::milliseconds(62));
  EXPECT_EQ(0, token_bucket.consume(1, true));
  time_system_.advanceTimeWait(std::chrono::milliseconds(1));
  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
}

// Test reset functionality for a shared token bucket.
TEST_F(SharedTokenBucketImplTest, Reset) {
  SharedTokenBucketImpl token_bucket{16, time_system_, 16};
  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post checking reset_once flag.
  token_bucket.synchronizer().waitOn(SharedTokenBucketImpl::ResetCheckSyncPoint);
  std::thread thread([&] { token_bucket.reset(1); });

  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(SharedTokenBucketImpl::ResetCheckSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(token_bucket.isMutexLocked());
  token_bucket.synchronizer().signal(SharedTokenBucketImpl::ResetCheckSyncPoint);

  thread.join();
  EXPECT_FALSE(token_bucket.isMutexLocked());

  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());

  // Reset again. Should be ignored for shared bucket.
  token_bucket.reset(5);
  EXPECT_EQ(0, token_bucket.consume(5, true));
}

// Verifies that TokenBucket can consume tokens with thread safety.
TEST_F(SharedTokenBucketImplTest, SynchronizedConsume) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 1};

  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post lock.
  token_bucket.synchronizer().waitOn(SharedTokenBucketImpl::GetImplSyncPoint);
  std::thread thread([&] { EXPECT_EQ(10, token_bucket.consume(20, true)); });

  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(SharedTokenBucketImpl::GetImplSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(token_bucket.isMutexLocked());
  token_bucket.synchronizer().signal(SharedTokenBucketImpl::GetImplSyncPoint);
  thread.join();
  EXPECT_FALSE(token_bucket.isMutexLocked());
}

TEST_F(SharedTokenBucketImplTest, SynchronizedNextTokenAvailable) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 16};

  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post lock.
  token_bucket.synchronizer().waitOn(SharedTokenBucketImpl::GetImplSyncPoint);
  std::thread thread(
      [&] { EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable()); });

  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(SharedTokenBucketImpl::GetImplSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(token_bucket.isMutexLocked());
  token_bucket.synchronizer().signal(SharedTokenBucketImpl::GetImplSyncPoint);
  thread.join();
  EXPECT_FALSE(token_bucket.isMutexLocked());
}

} // namespace Envoy
