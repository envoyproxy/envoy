#include "source/extensions/tracers/datadog/time_util.h"

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

datadog::tracing::TimePoint estimateTime(SystemTime wall) {
  auto point = datadog::tracing::default_clock();
  auto elapsed = point.wall - wall;
  // We could be off by a second or so if the system clock has been adjusted
  // since `wall` was taken. It's fine.
  point.tick -= elapsed;
  point.wall = wall;
  return point;
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
