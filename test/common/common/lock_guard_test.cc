#include "common/common/lock_guard.h"
#include "common/common/thread.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Thread {
namespace {

class LockGuardTest : public testing::Test {
protected:
  LockGuardTest() = default;
  int a_ ABSL_GUARDED_BY(a_mutex_){0};
  MutexBasicLockable a_mutex_;
  int b_{0};
};

TEST_F(LockGuardTest, TestLockGuard) {
  LockGuard lock(a_mutex_);
  EXPECT_EQ(1, ++a_);
}

TEST_F(LockGuardTest, TestOptionalLockGuard) {
  OptionalLockGuard lock(nullptr);
  EXPECT_EQ(1, ++b_);
}

TEST_F(LockGuardTest, TestReleasableLockGuard) {
  ReleasableLockGuard lock(a_mutex_);
  EXPECT_EQ(1, ++a_);
  lock.release();
}

TEST_F(LockGuardTest, TestTryLockGuard) {
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

} // namespace
} // namespace Thread
} // namespace Envoy
