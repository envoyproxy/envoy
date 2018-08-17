#pragma once

#include "common/common/utility.h"

namespace Envoy {

class TestTime {
public:
  TestTime();

  TimeSource& timeSource() { return time_source_; }

  // TODO(#4160): Add a 'mode' enum arg to the constructor, which
  // instantiates mock or perhaps fake time here rather than real-time, which is
  // makes testing non-deterministic and hard to debug. It should be easy, on
  // a test-by-test basis, to switch to mock time.
  ProdSystemTimeSource system_time_;
  ProdMonotonicTimeSource monotonic_time_;
  TimeSource time_source_;
};

} // namespace Envoy
