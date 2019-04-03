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
// clang-format off
#define ALL_DUBBO_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                          \
  COUNTER(request)                                                                                 \
  COUNTER(request_twoway)                                                                          \
  COUNTER(request_oneway)                                                                          \
  COUNTER(request_event)                                                                           \
  COUNTER(request_invalid_type)                                                                    \
  COUNTER(request_decoding_error)                                                                  \
  GAUGE(request_active)                                                                            \
  HISTOGRAM(request_time_ms)                                                                       \
  COUNTER(response)                                                                                \
  COUNTER(response_success)                                                                        \
  COUNTER(response_error)                                                                          \
  COUNTER(response_exception)                                                                      \
  COUNTER(response_decoding_error)                                                                 \
  COUNTER(cx_destroy_local_with_active_rq)                                                         \
  COUNTER(cx_destroy_remote_with_active_rq)                                                        \
  COUNTER(downstream_flow_control_paused_reading_total)                                            \
  COUNTER(downstream_flow_control_resumed_reading_total)                                           \
// clang-format on

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
