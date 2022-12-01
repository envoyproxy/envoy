#include "source/common/common/thread.h"

#include "gtest/gtest.h"
#include "library/common/thread/lock_guard.h"

namespace Envoy {
namespace Thread {

class ThreadTest : public testing::Test {
protected:
  ThreadTest() = default;
  int a_ ABSL_GUARDED_BY(a_mutex_){0};
  MutexBasicLockable a_mutex_;
  int b_{0};
};

TEST_F(ThreadTest, TestOptionalReleasableLockGuard) {
  OptionalReleasableLockGuard lock(nullptr);
  EXPECT_EQ(1, ++b_);

  OptionalReleasableLockGuard lock2(&a_mutex_);
  EXPECT_EQ(1, ++a_);
  lock2.release();
}

} // namespace Thread
} // namespace Envoy
