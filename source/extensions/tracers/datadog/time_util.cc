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
  auto point = clock();
  auto elapsed = point.wall - wall;
  if (elapsed > std::chrono::system_clock::duration::zero()) {
    point.tick -= elapsed;
  }
  point.wall = wall;
  return point;
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
