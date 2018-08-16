#pragma once

#include "common/common/utility.h"

namespace Envoy {

class TestTime {
public:
  TestTime();

  TimeSource& timeSource() { return time_source_; }

  // TODO(jmarantz): Switch these to using mock-time.
  ProdSystemTimeSource system_time_;
  ProdMonotonicTimeSource monotonic_time_;
  TimeSource time_source_;
};

} // namespace Envoy
