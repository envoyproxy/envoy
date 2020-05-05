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

void TestRealTimeSystem::advanceTimeAsync(const Duration& duration) { advanceTimeWait(duration); }

Thread::CondVar::WaitStatus TestRealTimeSystem::waitFor(Thread::MutexBasicLockable& lock,
                                                        Thread::CondVar& condvar,
                                                        const Duration& duration) noexcept {
  only_one_thread_.checkOneThread();
  return condvar.waitFor(lock, duration);
}

SystemTime TestRealTimeSystem::systemTime() { return real_time_system_.systemTime(); }

MonotonicTime TestRealTimeSystem::monotonicTime() { return real_time_system_.monotonicTime(); }

} // namespace Event
} // namespace Envoy
