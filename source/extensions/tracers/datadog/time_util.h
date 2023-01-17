#pragma once

#include <datadog/clock.h>

#include "envoy/common/time.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

// Return the `datadog::tracing::TimePoint` that most closely matches the specified
// `SystemTime`.
datadog::tracing::TimePoint estimateTime(SystemTime);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
