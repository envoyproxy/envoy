#include "common/common/thread.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {

class ThreadTest : public testing::Test {
protected:
  ThreadTest() : a_(0), b_(0) {}
  int a_ GUARDED_BY(a_mutex_);
  MutexBasicLockable a_mutex_;
  int b_;
};

TEST_F(ThreadTest, TestLockGuard) {
  LockGuard lock(a_mutex_);
  EXPECT_EQ(1, ++a_);
}

TEST_F(ThreadTest, TestOptionalLockGuard) {
  OptionalLockGuard lock(nullptr);
  EXPECT_EQ(1, ++b_);
}

TEST_F(ThreadTest, TestReleasableLockGuard) {
  ReleasableLockGuard lock(a_mutex_);
  EXPECT_EQ(1, ++a_);
  lock.release();
}

TEST_F(ThreadTest, TestTryLockGuard) {
  TryLockGuard lock(a_mutex_);

  if (lock.tryLock()) {
    // This test doesn't work, because a_mutex_ is guarded, and thread
    // annotations don't work with TryLockGuard. The macro is defined in
    // include/envoy/thread/thread.h.
    DISABLE_TRYLOCKGUARD_ANNOTATION(EXPECT_EQ(1, ++a_));

    // TryLockGuard does functionally work with unguarded variables.
    EXPECT_EQ(1, ++b_);
  }
}

} // namespace Thread
} // namespace Envoy
