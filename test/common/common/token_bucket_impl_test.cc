#include <chrono>

#include "common/common/token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {

class TokenBucketImplTest : public testing::Test {
protected:
  Event::SimulatedTimeSystem time_system_;

  bool isMutexLocked(Thread::MutexBasicLockable& mutex) {
    auto locked = mutex.tryLock();
    if (locked) {
      mutex.unlock();
    }
    return !locked;
  }
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
  token_bucket.reset(1);
  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());

  // Reset again. Should be honored.
  token_bucket.reset(5);
  EXPECT_EQ(5, token_bucket.consume(5, true));
}

// Verifies that TokenBucket can consume tokens with thread safety.
TEST_F(TokenBucketImplTest, SharedBucketSynchronizedConsume) {
  Thread::MutexBasicLockable mutex;
  TokenBucketImpl token_bucket{10, time_system_, 1, &mutex};

  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post lock.
  token_bucket.synchronizer().waitOn(TokenBucketImpl::MutexLockedSyncPoint);
  std::thread thread([&] { EXPECT_EQ(10, token_bucket.consume(20, true)); });

  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(TokenBucketImpl::MutexLockedSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(mutex));
  token_bucket.synchronizer().signal(TokenBucketImpl::MutexLockedSyncPoint);
  thread.join();
  EXPECT_FALSE(isMutexLocked(mutex));
}

TEST_F(TokenBucketImplTest, SharedBucketNextTokenAvailable) {
  Thread::MutexBasicLockable mutex;
  TokenBucketImpl token_bucket{10, time_system_, 16, &mutex};

  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post lock.
  token_bucket.synchronizer().waitOn(TokenBucketImpl::MutexLockedSyncPoint);
  std::thread thread(
      [&] { EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable()); });

  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(TokenBucketImpl::MutexLockedSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(mutex));
  token_bucket.synchronizer().signal(TokenBucketImpl::MutexLockedSyncPoint);
  thread.join();
  EXPECT_FALSE(isMutexLocked(mutex));
}

// Test reset functionality for a shared token bucket.
TEST_F(TokenBucketImplTest, SharedBucketReset) {
  Thread::MutexBasicLockable mutex;
  TokenBucketImpl token_bucket{16, time_system_, 16, &mutex};
  token_bucket.synchronizer().enable();
  // Start a thread and call consume. This will wait post lock.
  token_bucket.synchronizer().waitOn(TokenBucketImpl::MutexLockedSyncPoint);
  std::thread thread([&] { token_bucket.reset(1); });
  // Wait until the thread is actually waiting.
  token_bucket.synchronizer().barrierOn(TokenBucketImpl::MutexLockedSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(mutex));
  token_bucket.synchronizer().signal(TokenBucketImpl::MutexLockedSyncPoint);
  thread.join();
  EXPECT_FALSE(isMutexLocked(mutex));

  EXPECT_EQ(1, token_bucket.consume(2, true));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());

  // Reset again. Should be ignored for shared bucket.
  token_bucket.reset(5);
  EXPECT_EQ(0, token_bucket.consume(5, true));
}

} // namespace Envoy
