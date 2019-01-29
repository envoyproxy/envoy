#include "test/test_common/test_time.h"

#include "common/common/utility.h"

#include "test/test_common/global.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime() {}

namespace Event {

void TestRealTimeSystem::sleep(const Duration& duration) { std::this_thread::sleep_for(duration); }

Thread::CondVar::WaitStatus TestRealTimeSystem::waitFor(Thread::MutexBasicLockable& lock,
                                                        Thread::CondVar& condvar,
                                                        const Duration& duration) noexcept {
  return condvar.waitFor(lock, duration);
}

SystemTime TestRealTimeSystem::systemTime() { return real_time_system_.systemTime(); }

MonotonicTime TestRealTimeSystem::monotonicTime() { return real_time_system_.monotonicTime(); }

} // namespace Event
} // namespace Envoy
