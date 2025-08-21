#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

#define DATADOG_TRACER_STATS(COUNTER)                                                              \
  COUNTER(reports_skipped_no_cluster)                                                              \
  COUNTER(reports_sent)                                                                            \
  COUNTER(reports_dropped)                                                                         \
  COUNTER(reports_failed)

struct TracerStats {
  DATADOG_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

inline TracerStats makeTracerStats(Stats::Scope& scope) {
  return TracerStats{DATADOG_TRACER_STATS(POOL_COUNTER_PREFIX(scope, "tracing.datadog."))};
}

#undef DATADOG_TRACER_STATS

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
