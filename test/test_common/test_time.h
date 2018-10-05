#pragma once

#include "common/event/real_time_system.h"

#include "test/test_common/test_time_system.h"

namespace Envoy {
namespace Event {

class TestRealTimeSystem : public TestTimeSystem {
public:
  // TestTimeSystem
  void sleep(const Duration& duration) override;
  Thread::CondVar::WaitStatus
  waitFor(Thread::MutexBasicLockable& mutex, Thread::CondVar& condvar,
          const Duration& duration) noexcept EXCLUSIVE_LOCKS_REQUIRED(mutex) override;

  // Event::TimeSystem
  Event::SchedulerPtr createScheduler(Event::Libevent::BasePtr& libevent) override {
    return real_time_system_.createScheduler(libevent);
  }

  // TimeSource
  SystemTime systemTime() override { return real_time_system_.systemTime(); }
  MonotonicTime monotonicTime() override { return real_time_system_.monotonicTime(); }

private:
  Event::RealTimeSystem real_time_system_;
};

} // namespace Event

// Instantiates real-time sources for testing purposes. In general, this is a
// bad idea, and tests should use simulated or mock time.
//
// TODO(#4160): change all references to this class to instantiate instead to
// some kind of mock or simulated-time source.
class DangerousDeprecatedTestTime {
public:
  DangerousDeprecatedTestTime();

  Event::TestTimeSystem& timeSystem() { return time_system_; }

private:
  Event::TestRealTimeSystem time_system_;
};

} // namespace Envoy
