#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace SipProxy {

/**
 * All sip filter stats. @see stats_macros.h
 */
#define ALL_SIP_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                            \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(request)                                                                                 \
  COUNTER(response)                                                                                \
  COUNTER(response_error)                                                                          \
  COUNTER(response_exception)                                                                      \
  COUNTER(response_reply)                                                                          \
  COUNTER(response_success)                                                                        \
  GAUGE(request_active, Accumulate)                                                                \
  HISTOGRAM(request_time_ms, Milliseconds)

/**
 * Struct definition for all sip proxy stats. @see stats_macros.h
 */
struct SipFilterStats {
  ALL_SIP_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)

  static SipFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return SipFilterStats{ALL_SIP_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                               POOL_GAUGE_PREFIX(scope, prefix),
                                               POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

} // namespace SipProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
