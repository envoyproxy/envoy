#pragma once

#include "common/event/real_time_system.h"

namespace Envoy {

// Instantiates real-time sources for testing purposes. In general, this is a
// bad idea, and tests should use simulated or mock time.
//
// TODO(#4160): change all references to this class to instantiate instead to
// some kind of mock or simulated-time source.
class DangerousDeprecatedTestTime {
public:
  DangerousDeprecatedTestTime();

  Event::TimeSystem& timeSystem() { return time_system_; }

private:
  Event::RealTimeSystem time_system_;
};

} // namespace Envoy
