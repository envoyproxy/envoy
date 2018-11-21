#include "test/test_common/contention.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Thread {
namespace TestUtil {

void ContentionGenerator::generateContention(MutexTracerImpl& tracer) {
  MutexBasicLockable mu;
  Envoy::Thread::ThreadPtr t1 = launchThread(tracer, &mu);
  Envoy::Thread::ThreadPtr t2 = launchThread(tracer, &mu);
  t1->join();
  t2->join();
}

Envoy::Thread::ThreadPtr ContentionGenerator::launchThread(MutexTracerImpl& tracer,
                                                           MutexBasicLockable* mu) {
  return threadFactoryForTest().createThread(
      [&tracer, mu]() -> void { holdUntilContention(tracer, mu); });
}

void ContentionGenerator::holdUntilContention(MutexTracerImpl& tracer, MutexBasicLockable* mu) {
  DangerousDeprecatedTestTime test_time;
  int64_t curr_num_contentions = tracer.numContentions();
  while (tracer.numContentions() == curr_num_contentions) {
    test_time.timeSystem().sleep(std::chrono::milliseconds(1));
    LockGuard lock(*mu);
    // We hold the lock 90% of the time to ensure both contention and eventual acquisition, which
    // is needed to bump numContentions().
    test_time.timeSystem().sleep(std::chrono::milliseconds(9));
  }
}

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
