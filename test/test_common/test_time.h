#pragma once

#include "common/common/utility.h"

namespace Envoy {

// Instantiates real-time sources for testing purposes. In general, this is a
// bad idea, and tests should use simulated or mock time.
//
// TODO(#4160): change all references to this class to instantiate instead to
// some kind of mock or simulated-time source.
class DangerousDeprecatedTestTime {
public:
  DangerousDeprecatedTestTime();

  TimeSource& timeSource() { return time_source_; }

private:
  RealTimeSource time_source_;
};

class SimulatedTestTime {
public:
  SimulatedTestTime();

  TimeSource& timeSource() { return time_source_; }

 private:

};


} // namespace Envoy
