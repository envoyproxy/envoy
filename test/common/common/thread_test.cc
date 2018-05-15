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
  if (lock.isLocked()) {
    EXPECT_EQ(1, ++a_);
  }
}

} // namespace Thread
} // namespace Envoy
