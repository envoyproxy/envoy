#pragma once

#include "envoy/event/timer.h"

#include "common/common/utility.h"

namespace Envoy {
namespace Event {

class SimulatedTimeSystem : public TimeSystem {
 public:
  // TimeSystem
  TimerFactoryPtr createTimerFactory(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override { return system_time_; }
  MonotonicTime monotonicTime() override { return monotonic_time_; }

  // Advances time forward by the specified duration, running any timers
  // that are scheduled to wake up.
  void sleep(std::chrono::duration);

private:
  RealTimeSource real_time_source_;
  MonotonicTime monotonic_time_;
  SystemTime system_time_;
};

} // namespace Event
} // namespace Envoy
