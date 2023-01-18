#pragma once

#include <datadog/clock.h>

#include "envoy/common/time.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

// Return the `datadog::tracing::TimePoint` that most closely matches the
// specified `SystemTime`. Use the optionally specified
// `datadog::tracing::Clock` to read the current time. If a clock is not
// specified, then the default clock is used.
datadog::tracing::TimePoint estimateTime(SystemTime);
datadog::tracing::TimePoint estimateTime(SystemTime, const datadog::tracing::Clock&);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
