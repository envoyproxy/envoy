#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

/**
 * All dubbo filter stats. @see stats_macros.h
 */
#define ALL_DUBBO_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                          \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(local_response_business_exception)                                                       \
  COUNTER(local_response_error)                                                                    \
  COUNTER(local_response_success)                                                                  \
  COUNTER(request)                                                                                 \
  COUNTER(request_decoding_error)                                                                  \
  COUNTER(request_decoding_success)                                                                \
  COUNTER(request_event)                                                                           \
  COUNTER(request_oneway)                                                                          \
  COUNTER(request_twoway)                                                                          \
  COUNTER(response)                                                                                \
  COUNTER(response_business_exception)                                                             \
  COUNTER(response_decoding_error)                                                                 \
  COUNTER(response_decoding_success)                                                               \
  COUNTER(response_error)                                                                          \
  COUNTER(response_error_caused_connection_close)                                                  \
  COUNTER(response_success)                                                                        \
  GAUGE(request_active, Accumulate)                                                                \
  HISTOGRAM(request_time_ms, Milliseconds)

/**
 * Struct definition for all dubbo proxy stats. @see stats_macros.h
 */
struct DubboFilterStats {
  ALL_DUBBO_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)

  static DubboFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return DubboFilterStats{ALL_DUBBO_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                   POOL_GAUGE_PREFIX(scope, prefix),
                                                   POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
