#include "test/test_common/test_time.h"

#include "common/common/utility.h"

#include "test/test_common/global.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime() = default;

namespace Event {

TestTimeSystem& GlobalTimeSystem::timeSystem() {
  // TODO(#4160): Switch default to SimulatedTimeSystem.
  auto make_real_time_system = []() -> std::unique_ptr<TestTimeSystem> {
    return std::make_unique<TestRealTimeSystem>();
  };
  return singleton_->timeSystem(make_real_time_system);
}

void TestRealTimeSystem::advanceTimeWait(const Duration& duration) {
  only_one_thread_.checkOneThread();
  std::this_thread::sleep_for(duration);
}

bool TestRealTimeSystem::await(const bool& condition, Thread::MutexBasicLockable& mutex,
                               const Duration& timeout) {
  return mutex.awaitWithTimeout(condition, timeout);
  /*
  const MonotonicTime end_time = monotonicTime() + timeout;
  do {
    if (mutex.awaitWithTimeout(condition, std::chrono::milliseconds(5))) {
      return true;
    }
  } while (monotonicTime() < end_time);
  return false;
  */
}

bool TestRealTimeSystem::await(BoolFn check_condition, Thread::MutexBasicLockable& mutex,
                               const Duration& timeout) {
  const MonotonicTime end_time = monotonicTime() + timeout;
  do {
    if (mutex.awaitWithTimeout(check_condition, std::chrono::milliseconds(5))) {
      return true;
    }
  } while (monotonicTime() < end_time);
  return false;
}

void TestRealTimeSystem::advanceTimeAsync(const Duration& duration) { advanceTimeWait(duration); }

void TestRealTimeSystem::waitFor(Thread::MutexBasicLockable& lock, Thread::CondVar& condvar,
                                 const Duration& duration) noexcept {
  only_one_thread_.checkOneThread();
  condvar.waitFor(lock, duration);
}

SystemTime TestRealTimeSystem::systemTime() { return real_time_system_.systemTime(); }

MonotonicTime TestRealTimeSystem::monotonicTime() { return real_time_system_.monotonicTime(); }

} // namespace Event
} // namespace Envoy
