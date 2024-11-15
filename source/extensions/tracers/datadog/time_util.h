#pragma once

/**
 * This file contains functions related to time points and durations.
 *
 * Envoy has a TimeSource abstraction that provides both a system time point and
 * a steady ("monotonic") time point. However, only the system time is exposed
 * to the tracing subsystem. This may be remedied in the future, but for now we
 * work with the system time.
 *
 * This is problematic for the Datadog core tracing library (dd-trace-cpp),
 * because it uses the steady time to calculate the duration of a span
 * (end.tick - begin.tick). So, we need to get a steady clock time from a given system
 * clock time. The scheme is to measure the current system/steady time,
 * compare the system part with the given system time, and then adjust the
 * measured steady time accordingly. This is correct if the system clock has not
 * been adjusted since the given system time was measured. It's incorrect
 * otherwise, hence only an estimate. This conversion is performed by the
 * estimateTime function.
 */

#include "envoy/common/time.h"

#include "datadog/clock.h"

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
