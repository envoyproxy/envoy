#include "envoy/thread/thread.h"

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {
namespace {

struct ThreadFactorySingletonTest {
  ThreadFactorySingletonTest()
      : run_tid_(Envoy::Thread::ThreadFactorySingleton::get().currentThreadId()) {}

  void checkThreadId() const { ASSERT(run_tid_->isCurrentThreadId()); };

  ThreadIdPtr run_tid_;
};

// Verify that Thread::threadFactorySingleton is defined and initialized for tests.
TEST(ThreadFactorySingleton, BasicDeathTest) {
  ::testing::FLAGS_gtest_death_test_style = "threadsafe";

  // Verify that an ASSERT() will trigger due to a thread ID mismatch.
  ThreadFactorySingletonTest singleton_test;
  // Use std::thread instead of the ThreadFactory's createThread() to avoid the dependency on the
  // code under test.
  std::thread thread([&singleton_test]() {
    EXPECT_DEBUG_DEATH(singleton_test.checkThreadId(),
                       "assert failure: run_tid_->isCurrentThreadId()");
  });
  thread.join();
}

} // namespace
} // namespace Thread
} // namespace Envoy
