#include <chrono>
#include <thread>

#include "common/common/lock_guard.h"
#include "common/common/mutex_tracer.h"

#include "test/test_common/test_time.h"

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
  DangerousDeprecatedTestTime time_system_;
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

void holdUntilContention(absl::Mutex* mu, MutexTracer* tracer, DangerousDeprecatedTestTime* time) {
  int64_t curr_num_contentions = tracer->numContentions();
  while (tracer->numContentions() == curr_num_contentions) {
    time->timeSystem().sleep(std::chrono::milliseconds(1));
    mu->Lock();
    time->timeSystem().sleep(std::chrono::milliseconds(9));
    mu->Unlock();
  }
}

TEST_F(MutexTracerTest, TwoThreadsWithContention) {
  for (int i = 1; i <= 10; ++i) {
    int64_t curr_num_lifetime_wait_cycles = tracer_->lifetimeWaitCycles();
    std::thread t1(holdUntilContention, &mu_, tracer_, &time_system_);
    std::thread t2(holdUntilContention, &mu_, tracer_, &time_system_);
    t1.join();
    t2.join();

    EXPECT_EQ(tracer_->numContentions(), i);
    EXPECT_GT(tracer_->currentWaitCycles(), 0); // This shouldn't be hardcoded.
    EXPECT_GT(tracer_->lifetimeWaitCycles(), 0);
    EXPECT_GT(tracer_->lifetimeWaitCycles(), curr_num_lifetime_wait_cycles);
  }
}

} // namespace Envoy
