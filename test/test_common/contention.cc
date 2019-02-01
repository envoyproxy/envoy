#include "test/test_common/contention.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace Thread {
namespace TestUtil {

void ContentionGenerator::generateContention(MutexTracerImpl& tracer) {
  Envoy::Thread::ThreadPtr t1 = launchThread(tracer);
  Envoy::Thread::ThreadPtr t2 = launchThread(tracer);
  t1->join();
  t2->join();
}

Envoy::Thread::ThreadPtr ContentionGenerator::launchThread(MutexTracerImpl& tracer) {
  return threadFactoryForTest().createThread(
      [&tracer, this]() -> void { holdUntilContention(tracer); });
}

void ContentionGenerator::holdUntilContention(MutexTracerImpl& tracer) {
  int64_t curr_num_contentions = tracer.numContentions();
  while (tracer.numContentions() == curr_num_contentions) {
    test_time_.timeSystem().sleep(std::chrono::milliseconds(1));
    LockGuard lock(mutex_);
    // We hold the lock 90% of the time to ensure both contention and eventual acquisition, which
    // is needed to bump numContentions().
    test_time_.timeSystem().sleep(std::chrono::milliseconds(9));
  }
}

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
