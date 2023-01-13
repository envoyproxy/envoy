#pragma once

#include <datadog/clock.h>

#include "envoy/common/time.h"

#include "source/extensions/tracers/datadog/dd.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

// Return the `dd::TimePoint` that most closely matches the specified
// `SystemTime`.
dd::TimePoint estimateTime(SystemTime);

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
