#include <chrono>

#include "source/common/common/shared_token_bucket_impl.h"

#include "test/test_common/simulated_time_system.h"

#include "gtest/gtest.h"

namespace Envoy {

class SharedTokenBucketImplTest : public testing::Test {
protected:
  bool isMutexLocked(SharedTokenBucketImpl& token) {
    auto locked = token.mutex_.tryLock();
    if (locked) {
      token.mutex_.unlock();
    }
    return !locked;
  }

  Thread::ThreadSynchronizer& synchronizer(SharedTokenBucketImpl& token) {
    return token.synchronizer_;
  };

  Event::SimulatedTimeSystem time_system_;
  std::chrono::milliseconds time_to_next_token;
};

// Verifies TokenBucket initialization.
TEST_F(SharedTokenBucketImplTest, Initialization) {
  SharedTokenBucketImpl token_bucket{1, time_system_, -1.0};

  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));
}

// Verifies TokenBucket's maximum capacity.
TEST_F(SharedTokenBucketImplTest, MaxBucketSize) {
  SharedTokenBucketImpl token_bucket{3, time_system_, 1};

  EXPECT_EQ(3, token_bucket.consume(3, false, time_to_next_token));
  time_system_.setMonotonicTime(std::chrono::seconds(10));
  EXPECT_EQ(0, token_bucket.consume(4, false, time_to_next_token));
  EXPECT_EQ(3, token_bucket.consume(3, false, time_to_next_token));
}

// Verifies that TokenBucket can consume tokens.
TEST_F(SharedTokenBucketImplTest, Consume) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 1};

  EXPECT_EQ(0, token_bucket.consume(20, false, time_to_next_token));
  EXPECT_EQ(9, token_bucket.consume(9, false, time_to_next_token));

  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));

  time_system_.setMonotonicTime(std::chrono::milliseconds(999));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));

  time_system_.setMonotonicTime(std::chrono::milliseconds(5999));
  EXPECT_EQ(0, token_bucket.consume(6, false, time_to_next_token));

  time_system_.setMonotonicTime(std::chrono::milliseconds(6000));
  EXPECT_EQ(6, token_bucket.consume(6, false, time_to_next_token));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));
}

// Verifies that TokenBucket can refill tokens.
TEST_F(SharedTokenBucketImplTest, Refill) {
  SharedTokenBucketImpl token_bucket{1, time_system_, 0.5};
  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));

  time_system_.setMonotonicTime(std::chrono::milliseconds(500));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));
  time_system_.setMonotonicTime(std::chrono::milliseconds(1500));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));
  time_system_.setMonotonicTime(std::chrono::milliseconds(2000));
  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));
}

TEST_F(SharedTokenBucketImplTest, NextTokenAvailable) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 5};
  EXPECT_EQ(9, token_bucket.consume(9, false, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable());
  EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));
  EXPECT_EQ(0, token_bucket.consume(1, false, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(200), token_bucket.nextTokenAvailable());
}

// Test partial consumption of tokens.
TEST_F(SharedTokenBucketImplTest, PartialConsumption) {
  SharedTokenBucketImpl token_bucket{16, time_system_, 16};
  EXPECT_EQ(16, token_bucket.consume(18, true, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
  time_system_.advanceTimeWait(std::chrono::milliseconds(62));
  EXPECT_EQ(0, token_bucket.consume(1, true, time_to_next_token));
  time_system_.advanceTimeWait(std::chrono::milliseconds(1));
  EXPECT_EQ(1, token_bucket.consume(2, true, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());
}

// Test reset functionality for a shared token bucket.
TEST_F(SharedTokenBucketImplTest, Reset) {
  SharedTokenBucketImpl token_bucket{16, time_system_, 16};
  synchronizer(token_bucket).enable();
  // Start a thread and call consume. This will wait post checking reset_once flag.
  synchronizer(token_bucket).waitOn(SharedTokenBucketImpl::ResetCheckSyncPoint);
  std::thread thread([&] { token_bucket.maybeReset(1); });

  // Wait until the thread is actually waiting.
  synchronizer(token_bucket).barrierOn(SharedTokenBucketImpl::ResetCheckSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(token_bucket));
  synchronizer(token_bucket).signal(SharedTokenBucketImpl::ResetCheckSyncPoint);

  thread.join();
  EXPECT_FALSE(isMutexLocked(token_bucket));

  EXPECT_EQ(1, token_bucket.consume(2, true, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(63), token_bucket.nextTokenAvailable());

  // Reset again. Should be ignored for shared bucket.
  token_bucket.maybeReset(5);
  EXPECT_EQ(0, token_bucket.consume(5, true, time_to_next_token));
}

// Verifies that TokenBucket can consume tokens with thread safety.
TEST_F(SharedTokenBucketImplTest, SynchronizedConsume) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 1};

  synchronizer(token_bucket).enable();
  // Start a thread and call consume. This will wait post lock.
  synchronizer(token_bucket).waitOn(SharedTokenBucketImpl::GetImplSyncPoint);
  std::thread thread([&] { EXPECT_EQ(10, token_bucket.consume(20, true, time_to_next_token)); });

  // Wait until the thread is actually waiting.
  synchronizer(token_bucket).barrierOn(SharedTokenBucketImpl::GetImplSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(token_bucket));
  synchronizer(token_bucket).signal(SharedTokenBucketImpl::GetImplSyncPoint);
  thread.join();
  EXPECT_FALSE(isMutexLocked(token_bucket));
}

TEST_F(SharedTokenBucketImplTest, SynchronizedNextTokenAvailable) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 16};

  synchronizer(token_bucket).enable();
  // Start a thread and call consume. This will wait post lock.
  synchronizer(token_bucket).waitOn(SharedTokenBucketImpl::GetImplSyncPoint);
  std::thread thread(
      [&] { EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable()); });

  // Wait until the thread is actually waiting.
  synchronizer(token_bucket).barrierOn(SharedTokenBucketImpl::GetImplSyncPoint);

  // Mutex should be already locked.
  EXPECT_TRUE(isMutexLocked(token_bucket));
  synchronizer(token_bucket).signal(SharedTokenBucketImpl::GetImplSyncPoint);
  thread.join();
  EXPECT_FALSE(isMutexLocked(token_bucket));
}

// Verifies that TokenBucket can consume tokens with thread safety.
TEST_F(SharedTokenBucketImplTest, SynchronizedConsumeAndNextToken) {
  SharedTokenBucketImpl token_bucket{10, time_system_, 5};

  // Exhaust all tokens.
  EXPECT_EQ(10, token_bucket.consume(20, true, time_to_next_token));
  EXPECT_EQ(std::chrono::milliseconds(200), token_bucket.nextTokenAvailable());
  time_system_.advanceTimeWait(std::chrono::milliseconds(400));

  // Start a thread and call consume to refill tokens.
  std::thread t1([&] { EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token)); });

  t1.join();

  EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable());

  token_bucket.maybeReset(10);
  // Exhaust all tokens.
  std::chrono::milliseconds time_to_next_token(0);
  EXPECT_EQ(10, token_bucket.consume(20, true, time_to_next_token));
  EXPECT_EQ(time_to_next_token.count(), 200);
  time_system_.advanceTimeWait(std::chrono::milliseconds(400));

  // Start a thread and call consume to refill tokens.
  std::thread t2([&] {
    EXPECT_EQ(1, token_bucket.consume(1, false, time_to_next_token));
    EXPECT_EQ(time_to_next_token.count(), 0);
  });

  t2.join();

  EXPECT_EQ(std::chrono::milliseconds(0), token_bucket.nextTokenAvailable());
}

} // namespace Envoy
