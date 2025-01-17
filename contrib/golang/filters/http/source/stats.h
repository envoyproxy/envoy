#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Golang {

/**
 * All golang filter stats. @see stats_macros.h
 */
#define ALL_GOLANG_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM) COUNTER(panic_error)

/**
 * Struct definition for all golang proxy stats. @see stats_macros.h
 */
struct GolangFilterStats {
  ALL_GOLANG_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)

  static GolangFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return GolangFilterStats{ALL_GOLANG_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                     POOL_GAUGE_PREFIX(scope, prefix),
                                                     POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};
} // namespace Golang
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
