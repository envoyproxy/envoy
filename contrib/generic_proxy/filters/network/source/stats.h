#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

/**
 * All generic filter stats. @see stats_macros.h
 */
#define ALL_GENERIC_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                        \
  COUNTER(request)                                                                                 \
  COUNTER(request_decoding_error)                                                                  \
  COUNTER(response)                                                                                \
  COUNTER(response_decoding_error)                                                                 \
  GAUGE(request_active, Accumulate)                                                                \
  HISTOGRAM(request_time_ms, Milliseconds)

/**
 * Struct definition for all generic proxy stats. @see stats_macros.h
 */
struct GenericFilterStats {
  ALL_GENERIC_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                           GENERATE_HISTOGRAM_STRUCT)

  static GenericFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return GenericFilterStats{ALL_GENERIC_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                       POOL_GAUGE_PREFIX(scope, prefix),
                                                       POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
