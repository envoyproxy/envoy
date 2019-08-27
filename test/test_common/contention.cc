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
  Event::DispatcherPtr dispatcher = api_.allocateDispatcher();
  Event::TimerPtr timer = dispatcher->createTimer([&dispatcher]() { dispatcher->exit(); });
  auto sleep_ms = [&timer, &dispatcher](int num_ms) {
    timer->enableTimer(std::chrono::milliseconds(num_ms));
    dispatcher->run(Event::Dispatcher::RunType::RunUntilExit);
  };
  int64_t curr_num_contentions = tracer.numContentions();
  do {
    sleep_ms(1);
    {
      LockGuard lock(mutex_);
      // We hold the lock 90% of the time to ensure both contention and eventual acquisition, which
      // is needed to bump numContentions().
      sleep_ms(9);
    }
    if (tracer.numContentions() > curr_num_contentions) {
      found_contention_ = true;
    }
  } while (!found_contention_);
}

} // namespace TestUtil
} // namespace Thread
} // namespace Envoy
