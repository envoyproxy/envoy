#pragma once

#include "common/common/utility.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Event {

/**
 * Real-time implementation of TimeSystem.
 */
class RealTimeSystem : public TimeSystem {
public:
  // TimeSystem
  TimerFactoryPtr createTimerFactory(Libevent::BasePtr&) override;

  // TimeSource
  SystemTime systemTime() override { return time_source_.systemTime(); }
  MonotonicTime monotonicTime() override { return time_source_.monotonicTime(); }

 private:
  RealTimeSource time_source_;
};

} // namespace Event
} // namespace Envoy
