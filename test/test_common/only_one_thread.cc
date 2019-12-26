#include "test/test_common/only_one_thread.h"

#include "envoy/thread/thread.h"

#include "common/common/lock_guard.h"

#include "test/test_common/thread_factory_for_test.h"

namespace Envoy {
namespace Thread {

OnlyOneThread::OnlyOneThread() : thread_factory_(threadFactoryForTest()) {}

void OnlyOneThread::checkOneThread() {
  LockGuard lock(mutex_);
  if (thread_advancing_time_.isEmpty()) {
    thread_advancing_time_ = thread_factory_.currentThreadId();
  } else {
    RELEASE_ASSERT(thread_advancing_time_ == thread_factory_.currentThreadId(),
                   "time should only be advanced on one thread in the context of a test");
  }
}

} // namespace Thread
} // namespace Envoy
