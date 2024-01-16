#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

/**
 * All thrift filter stats. @see stats_macros.h
 */
#define ALL_THRIFT_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                         \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(downstream_cx_max_requests)                                                              \
  COUNTER(downstream_response_drain_close)                                                         \
  COUNTER(request)                                                                                 \
  COUNTER(request_call)                                                                            \
  COUNTER(request_decoding_error)                                                                  \
  COUNTER(request_invalid_type)                                                                    \
  COUNTER(request_oneway)                                                                          \
  COUNTER(request_passthrough)                                                                     \
  COUNTER(request_internal_error)                                                                  \
  COUNTER(response)                                                                                \
  COUNTER(response_decoding_error)                                                                 \
  COUNTER(response_error)                                                                          \
  COUNTER(response_exception)                                                                      \
  COUNTER(response_invalid_type)                                                                   \
  COUNTER(response_passthrough)                                                                    \
  COUNTER(response_reply)                                                                          \
  COUNTER(response_success)                                                                        \
  GAUGE(request_active, Accumulate)                                                                \
  HISTOGRAM(request_time_ms, Milliseconds)

/**
 * Struct definition for all thrift proxy stats. @see stats_macros.h
 */
struct ThriftFilterStats {
  ALL_THRIFT_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT)

  static ThriftFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return ThriftFilterStats{ALL_THRIFT_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                     POOL_GAUGE_PREFIX(scope, prefix),
                                                     POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
