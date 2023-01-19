#pragma once

#include <datadog/clock.h>

#include "envoy/common/time.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

/**
 * Convert a specified system \p time to a datadog time point, estimating the
    steady portion of the result by examining the current time as measured by
    the optionally specified \p clock and comparing it with the given \p time.
 * @param time system time to convert from
 * @param clock datadog clock used to measure the current time (default clock if omitted)
 * @return datadog time point whose steady portion is estimated from the given \p time.
 */
datadog::tracing::TimePoint estimateTime(SystemTime time);
datadog::tracing::TimePoint estimateTime(SystemTime time, const datadog::tracing::Clock& clock);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
