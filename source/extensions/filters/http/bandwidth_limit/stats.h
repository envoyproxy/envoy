#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthLimitFilter {

/**
 * All bandwidth limit stats. @see stats_macros.h
 */
#define ALL_BANDWIDTH_LIMIT_STATS(COUNTER, GAUGE, HISTOGRAM)                                       \
  COUNTER(request_enabled)                                                                         \
  COUNTER(response_enabled)                                                                        \
  COUNTER(request_enforced)                                                                        \
  COUNTER(response_enforced)                                                                       \
  GAUGE(request_pending, Accumulate)                                                               \
  GAUGE(response_pending, Accumulate)                                                              \
  GAUGE(request_incoming_size, Accumulate)                                                         \
  GAUGE(response_incoming_size, Accumulate)                                                        \
  GAUGE(request_allowed_size, Accumulate)                                                          \
  GAUGE(response_allowed_size, Accumulate)                                                         \
  COUNTER(request_incoming_total_size)                                                             \
  COUNTER(response_incoming_total_size)                                                            \
  COUNTER(request_allowed_total_size)                                                              \
  COUNTER(response_allowed_total_size)                                                             \
  HISTOGRAM(request_transfer_duration, Milliseconds)                                               \
  HISTOGRAM(response_transfer_duration, Milliseconds)

/**
 * Struct definition for all bandwidth limit stats. @see stats_macros.h
 */
struct BandwidthLimitStats {
  ALL_BANDWIDTH_LIMIT_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                            GENERATE_HISTOGRAM_STRUCT)
};

BandwidthLimitStats generateStats(absl::string_view prefix, Stats::Scope& scope);

} // namespace BandwidthLimitFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
