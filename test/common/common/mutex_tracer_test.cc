#include <chrono>
#include <thread>

#include "common/common/mutex_tracer.h"

#include "absl/synchronization/mutex.h"
#include "gtest/gtest.h"

namespace Envoy {

class MutexTracerTest : public testing::Test {
protected:
  void SetUp() {
    MutexTracer::GetTracer()->Reset();
    absl::RegisterMutexTracer(&MutexTracer::ContentionHook);
  }

  absl::Mutex mu_;
  MutexData data_;
};

// Call the contention hook manually.
TEST_F(MutexTracerTest, AddN) {
  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 0);
  EXPECT_EQ(data_.current_wait_cycles, 0);
  EXPECT_EQ(data_.lifetime_wait_cycles, 0);

  MutexTracer::GetTracer()->ContentionHook(nullptr, nullptr, 2);

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 1);
  EXPECT_EQ(data_.current_wait_cycles, 2);
  EXPECT_EQ(data_.lifetime_wait_cycles, 2);

  MutexTracer::GetTracer()->ContentionHook(nullptr, nullptr, 3);

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 2);
  EXPECT_EQ(data_.current_wait_cycles, 3);
  EXPECT_EQ(data_.lifetime_wait_cycles, 5);

  MutexTracer::GetTracer()->ContentionHook(nullptr, nullptr, 0);

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 3);
  EXPECT_EQ(data_.current_wait_cycles, 0);
  EXPECT_EQ(data_.lifetime_wait_cycles, 5);
}

void holdMutexForNMilliseconds(absl::Mutex* mu, int64_t duration) {
  mu->Lock();
  std::this_thread::sleep_for(std::chrono::milliseconds(duration));
  mu->Unlock();
}

// Call the contention hook in a real contention scenario.
TEST_F(MutexTracerTest, OneThreadNoContention) {
  // Regular operation doesn't cause contention.
  mu_.Lock();
  mu_.Unlock();

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 0);
  EXPECT_EQ(data_.current_wait_cycles, 0);
  EXPECT_EQ(data_.lifetime_wait_cycles, 0);
}

TEST_F(MutexTracerTest, TryLockNoContention) {
  // TryLocks don't cause contention.
  mu_.Lock();
  EXPECT_FALSE(mu_.TryLock());
  mu_.Unlock();

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 0);
  EXPECT_EQ(data_.current_wait_cycles, 0);
  EXPECT_EQ(data_.lifetime_wait_cycles, 0);
}

TEST_F(MutexTracerTest, TwoThreadsWithContention) {
  // Real mutex contention successfully triggers this callback.
  std::thread t1(holdMutexForNMilliseconds, &mu_, 10);
  std::thread t2(holdMutexForNMilliseconds, &mu_, 10);
  t1.join();
  t2.join();

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 1);
  EXPECT_GT(data_.current_wait_cycles, 0); // These shouldn't be hardcoded.
  EXPECT_GT(data_.lifetime_wait_cycles, 0);

  // If we store this for later,
  int64_t prev_lifetime_wait_cycles = data_.lifetime_wait_cycles;

  // Then on our next call...
  std::thread t3(holdMutexForNMilliseconds, &mu_, 10);
  std::thread t4(holdMutexForNMilliseconds, &mu_, 10);
  t3.join();
  t4.join();

  data_ = MutexTracer::GetTracer()->GetData();
  EXPECT_EQ(data_.num_contentions, 2);
  EXPECT_GT(data_.current_wait_cycles, 0);
  EXPECT_GT(data_.lifetime_wait_cycles, 0);
  // ...we can confirm that this lifetime value went up.
  EXPECT_GT(data_.lifetime_wait_cycles, prev_lifetime_wait_cycles);
}

} // namespace Envoy
