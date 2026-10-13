#pragma once

#include "source/common/event/real_time_system.h"

#include "test/test_common/global.h"
#include "test/test_common/test_time_system.h"

namespace Envoy {
namespace Event {

class TestRealTimeSystem : public TestTimeSystem {
public:
  // TestTimeSystem
  void advanceTimeAsyncImpl(const Duration& duration) override;
  void advanceTimeWaitImpl(const Duration& duration) override;

  // Event::TimeSystem
  Event::SchedulerPtr createScheduler(Scheduler& base_scheduler,
                                      CallbackScheduler& cb_scheduler) override {
    return real_time_system_.createScheduler(base_scheduler, cb_scheduler);
  }

  // TimeSource
  SystemTime systemTime() override;
  MonotonicTime monotonicTime() override;

private:
  Event::RealTimeSystem real_time_system_;
};

class GlobalTimeSystem : public DelegatingTestTimeSystemBase<TestTimeSystem> {
public:
  TestTimeSystem& timeSystem() override;

private:
  Test::Global<SingletonTimeSystemHelper> singleton_;
};

} // namespace Event

// Instantiates real-time sources for testing purposes. In general, this is a
// bad idea, and tests should use simulated or mock time.
//
// TODO(#4160): change most references to this class to SimulatedTimeSystem.
class DangerousDeprecatedTestTime {
public:
  DangerousDeprecatedTestTime();

  Event::TestTimeSystem& timeSystem() { return time_system_.timeSystem(); }

private:
  Event::DelegatingTestTimeSystem<Event::TestRealTimeSystem> time_system_;
};

} // namespace Envoy
