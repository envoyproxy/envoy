#pragma once

#include <string>

#include "envoy/common/time.h"

#include "extensions/tracers/xray/sampling_strategy.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {

class Tracer {
public:
  Tracer(absl::string_view segment_name, TimeSource& time_source)
      : segment_name_(segment_name), time_source_(time_source) {}

  /**
   * Starts a tracing span for XRay
   */
  Tracing::SpanPtr startSpan() { return nullptr; }

private:
  const std::string segment_name_;
  TimeSource& time_source_;
};

} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
