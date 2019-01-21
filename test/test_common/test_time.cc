#include "test/test_common/test_time.h"

#include "common/common/utility.h"

#include "test/test_common/global.h"
#include "test/test_common/simulated_time_system.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime() {}

namespace Event {

void TestRealTimeSystem::sleep(const Duration& duration) { std::this_thread::sleep_for(duration); }

Thread::CondVar::WaitStatus TestRealTimeSystem::waitFor(Thread::MutexBasicLockable& lock,
                                                        Thread::CondVar& condvar,
                                                        const Duration& duration) noexcept {
  return condvar.waitFor(lock, duration);
}

SystemTime TestRealTimeSystem::systemTime() {
  ASSERT(!SimulatedTimeSystem::hasInstance());
  return real_time_system_.systemTime();
}

MonotonicTime TestRealTimeSystem::monotonicTime() {
  ASSERT(!SimulatedTimeSystem::hasInstance());
  return real_time_system_.monotonicTime();
}

MockTimeSystem::MockTimeSystem() {}
MockTimeSystem::~MockTimeSystem() {}

// TODO(#4160): Eliminate all uses of MockTimeSystem, replacing with SimulatedTimeSystem,
// where timer callbacks are triggered by the advancement of time. This implementation
// matches recent behavior, where real-time timers were created directly in libevent
// by dispatcher_impl.cc.
Event::SchedulerPtr MockTimeSystem::createScheduler(Event::Libevent::BasePtr& base) {
  return real_time_system_.createScheduler(base);
}

void MockTimeSystem::sleep(const Duration& duration) { real_time_system_.sleep(duration); }

Thread::CondVar::WaitStatus
MockTimeSystem::waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
                        const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) {
  return real_time_system_.waitFor(mutex, condvar, duration);
}

} // namespace Event
} // namespace Envoy
