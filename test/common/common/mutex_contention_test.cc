#include <chrono>
#include <thread>

#include "common/common/mutex_contention.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {

class MutexContentionTest : public testing::Test {
protected:
  void SetUp() {
    mutexContentionReset();
    absl::RegisterMutexTracer(&mutexContentionCallback);
  }

  absl::Mutex mu_;
};

TEST_F(MutexContentionTest, AddN) {
  EXPECT_EQ(getNumContentions(), 0);
  EXPECT_EQ(getCurrentWaitCycles(), 0);
  EXPECT_EQ(getLifetimeWaitCycles(), 0);

  mutexContentionCallback(nullptr, nullptr, 2);

  EXPECT_EQ(getNumContentions(), 1);
  EXPECT_EQ(getCurrentWaitCycles(), 2);
  EXPECT_EQ(getLifetimeWaitCycles(), 2);

  mutexContentionCallback(nullptr, nullptr, 3);

  EXPECT_EQ(getNumContentions(), 2);
  EXPECT_EQ(getCurrentWaitCycles(), 3);
  EXPECT_EQ(getLifetimeWaitCycles(), 5);

  mutexContentionCallback(nullptr, nullptr, 0);

  EXPECT_EQ(getNumContentions(), 3);
  EXPECT_EQ(getCurrentWaitCycles(), 0);
  EXPECT_EQ(getLifetimeWaitCycles(), 5);
}

void holdMutex(absl::Mutex* mu, int64_t duration) {
  mu->Lock();
  std::this_thread::sleep_for(std::chrono::milliseconds(duration));
  mu->Unlock();
}

TEST_F(MutexContentionTest, OneThreadNoContention) {

  // Regular operation doesn't cause contention.
  mu_.Lock();
  mu_.Unlock();

  EXPECT_EQ(getNumContentions(), 0);
  EXPECT_EQ(getCurrentWaitCycles(), 0);
  EXPECT_EQ(getLifetimeWaitCycles(), 0);
}

TEST_F(MutexContentionTest, TryLockNoContention) {
  // TryLocks don't cause contention.
  mu_.Lock();
  EXPECT_FALSE(mu_.TryLock());
  mu_.Unlock();

  EXPECT_EQ(getNumContentions(), 0);
  EXPECT_EQ(getCurrentWaitCycles(), 0);
  EXPECT_EQ(getLifetimeWaitCycles(), 0);
}

TEST_F(MutexContentionTest, TwoThreadsWithContention) {
  // Real mutex contention successfully triggers this callback.
  std::thread t1(holdMutex, &mu_, 10);
  std::thread t2(holdMutex, &mu_, 10);
  t1.join();
  t2.join();

  EXPECT_EQ(getNumContentions(), 1);
  EXPECT_GT(getCurrentWaitCycles(), 0); // These shouldn't be hardcoded.
  EXPECT_GT(getLifetimeWaitCycles(), 0);

  // If we store this for later,
  int64_t curr_lifetime_wait_cycles = getLifetimeWaitCycles();

  // Then on our next call...
  std::thread t3(holdMutex, &mu_, 10);
  std::thread t4(holdMutex, &mu_, 10);
  t3.join();
  t4.join();

  EXPECT_EQ(getNumContentions(), 2);
  EXPECT_GT(getCurrentWaitCycles(), 0);
  EXPECT_GT(getLifetimeWaitCycles(), 0);
  // ...we can confirm that this lifetime value went up.
  EXPECT_GT(getLifetimeWaitCycles(), curr_lifetime_wait_cycles);
}

} // namespace Thread
} // namespace Envoy
