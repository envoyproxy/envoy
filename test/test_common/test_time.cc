#include "test/test_common/test_time.h"

#include "common/common/utility.h"

namespace Envoy {

DangerousDeprecatedTestTime::DangerousDeprecatedTestTime() {}

namespace Event {

void TestRealTimeSystem::sleep(const Duration& duration) { std::this_thread::sleep_for(duration); }

Thread::CondVar::WaitStatus TestRealTimeSystem::waitFor(Thread::MutexBasicLockable& lock,
                                                        Thread::CondVar& condvar,
                                                        const Duration& duration) noexcept {
  return condvar.waitFor(lock, duration);
}

} // namespace Event
} // namespace Envoy
