#include <chrono>
#include <thread>

#include "common/common/mutex_tracer.h"

#include "absl/synchronization/mutex.h"
#include "absl/synchronization/notification.h"
#include "gtest/gtest.h"

namespace Envoy {

class MutexTracerTest : public testing::Test {
protected:
  void SetUp() override {
    tracer_ = MutexTracer::getOrCreateTracer();
    tracer_->reset();
  }

  // Since MutexTracer::contentionHook is a private method, MutexTracerTest is a friend class.
  void sendWaitCyclesToContentionHook(int64_t wait_cycles) {
    tracer_->contentionHook(nullptr, nullptr, wait_cycles);
  }

  absl::Mutex mu_;
  MutexTracer* tracer_;
};

// Call the contention hook manually.
TEST_F(MutexTracerTest, AddN) {
  EXPECT_EQ(tracer_->numContentions(), 0);
  EXPECT_EQ(tracer_->currentWaitCycles(), 0);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 0);

  sendWaitCyclesToContentionHook(2);

  EXPECT_EQ(tracer_->numContentions(), 1);
  EXPECT_EQ(tracer_->currentWaitCycles(), 2);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 2);

  sendWaitCyclesToContentionHook(3);

  EXPECT_EQ(tracer_->numContentions(), 2);
  EXPECT_EQ(tracer_->currentWaitCycles(), 3);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 5);

  sendWaitCyclesToContentionHook(0);

  EXPECT_EQ(tracer_->numContentions(), 3);
  EXPECT_EQ(tracer_->currentWaitCycles(), 0);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 5);
}

// Call the contention hook in a real contention scenario.
TEST_F(MutexTracerTest, OneThreadNoContention) {
  // Regular operation doesn't cause contention.
  mu_.Lock();
  mu_.Unlock();

  EXPECT_EQ(tracer_->numContentions(), 0);
  EXPECT_EQ(tracer_->currentWaitCycles(), 0);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 0);
}

TEST_F(MutexTracerTest, TryLockNoContention) {
  // TryLocks don't cause contention.
  mu_.Lock();
  EXPECT_FALSE(mu_.TryLock());
  mu_.Unlock();

  EXPECT_EQ(tracer_->numContentions(), 0);
  EXPECT_EQ(tracer_->currentWaitCycles(), 0);
  EXPECT_EQ(tracer_->lifetimeWaitCycles(), 0);
}

void holdMutexUntil(absl::Mutex* mu, absl::Notification* notif_unlock) {
  mu->Lock();
  notif_unlock->WaitForNotification();
  mu->Unlock();
}

TEST_F(MutexTracerTest, TwoThreadsWithContention) {
  // Real mutex contention successfully triggers this callback.
  absl::Notification thread1_unlock;
  absl::Notification thread2_unlock;
  std::thread t1(holdMutexUntil, &mu_, &thread1_unlock);
  std::thread t2(holdMutexUntil, &mu_, &thread2_unlock);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  thread1_unlock.Notify();
  thread2_unlock.Notify();
  t1.join();
  t2.join();

  EXPECT_EQ(tracer_->numContentions(), 1);
  EXPECT_GT(tracer_->currentWaitCycles(), 0); // These shouldn't be hardcoded.
  EXPECT_GT(tracer_->lifetimeWaitCycles(), 0);

  // If we store this for later,
  int64_t prev_lifetime_wait_cycles = tracer_->lifetimeWaitCycles();

  // Then on our next call...
  absl::Notification thread3_unlock;
  absl::Notification thread4_unlock;
  std::thread t3(holdMutexUntil, &mu_, &thread3_unlock);
  std::thread t4(holdMutexUntil, &mu_, &thread4_unlock);
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  thread3_unlock.Notify();
  thread4_unlock.Notify();
  t3.join();
  t4.join();

  EXPECT_EQ(tracer_->numContentions(), 2);
  EXPECT_GT(tracer_->currentWaitCycles(), 0);
  EXPECT_GT(tracer_->lifetimeWaitCycles(), 0);
  // ...we can confirm that this lifetime value went up.
  EXPECT_GT(tracer_->lifetimeWaitCycles(), prev_lifetime_wait_cycles);
}

} // namespace Envoy
