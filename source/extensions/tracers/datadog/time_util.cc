#include "source/extensions/tracers/datadog/time_util.h"

#include <chrono>

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

datadog::tracing::TimePoint estimateTime(SystemTime wall) {
  return estimateTime(wall, datadog::tracing::default_clock);
}

datadog::tracing::TimePoint estimateTime(SystemTime wall, const datadog::tracing::Clock& clock) {
  datadog::tracing::TimePoint point = clock();
  if (point.wall > wall) {
    point.tick -= point.wall - wall;
  }
  point.wall = wall;
  return point;
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
