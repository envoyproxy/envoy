#include "envoy/thread/thread.h"

#include "common/common/assert.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {
namespace {

class ThreadFactorySingletonTest : public testing::Test {
protected:
  ThreadFactorySingletonTest()
      : run_tid_(Envoy::Thread::ThreadFactorySingleton::get().currentThreadId()) {}

  bool checkThreadId() const { return run_tid_->isCurrentThreadId(); };

  ThreadIdPtr run_tid_;
};

// Verify that Thread::threadFactorySingleton is defined and initialized for tests.
TEST_F(ThreadFactorySingletonTest, IsCurrentThread) {
  // Use std::thread instead of the ThreadFactory's createThread() to avoid the dependency on the
  // code under test.
  bool is_current = checkThreadId();
  EXPECT_TRUE(is_current);
  std::thread thread([this, &is_current]() { is_current = checkThreadId(); });
  thread.join();
  EXPECT_FALSE(is_current) << "run_tid_->isCurrentThreadId() from inside another thread";
}

} // namespace
} // namespace Thread
} // namespace Envoy
