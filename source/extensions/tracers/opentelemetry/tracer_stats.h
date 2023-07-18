#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

#define OPENTELEMETRY_TRACER_STATS(COUNTER)                                                        \
  COUNTER(http_reports_failed)                                                                     \
  COUNTER(http_reports_sent)                                                                       \
  COUNTER(http_reports_success)                                                                    \
  COUNTER(spans_sent)                                                                              \
  COUNTER(timer_flushed)

struct OpenTelemetryTracerStats {
  OPENTELEMETRY_TRACER_STATS(GENERATE_COUNTER_STRUCT)
};

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
