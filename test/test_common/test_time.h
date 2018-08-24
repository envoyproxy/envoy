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
  // TODO(#4160): Add a 'mode' enum arg to the constructor, which
  // instantiates mock or perhaps fake time here rather than real-time, which is
  // makes testing non-deterministic and hard to debug. It should be easy, on
  // a test-by-test basis, to switch to mock time.
  ProdSystemTimeSource system_time_;
  ProdMonotonicTimeSource monotonic_time_;
  TimeSource time_source_;
};

} // namespace Envoy
