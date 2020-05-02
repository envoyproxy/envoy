#pragma once

#include <string>

#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RocketmqProxy {

/**
 * All rocketmq filter stats. @see stats_macros.h
 */
#define ALL_ROCKETMQ_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM)                                       \
  COUNTER(request)                                                                                 \
  COUNTER(request_decoding_error)                                                                  \
  COUNTER(request_decoding_success)                                                                \
  COUNTER(response)                                                                                \
  COUNTER(response_decoding_error)                                                                 \
  COUNTER(response_decoding_success)                                                               \
  COUNTER(response_error)                                                                          \
  COUNTER(response_success)                                                                        \
  COUNTER(heartbeat)                                                                               \
  COUNTER(unregister)                                                                              \
  COUNTER(get_topic_route)                                                                         \
  COUNTER(send_message_v1)                                                                         \
  COUNTER(send_message_v2)                                                                         \
  COUNTER(pop_message)                                                                             \
  COUNTER(ack_message)                                                                             \
  COUNTER(get_consumer_list)                                                                       \
  COUNTER(maintenance_failure)                                                                     \
  GAUGE(request_active, Accumulate)                                                                \
  GAUGE(send_message_v1_active, Accumulate)                                                        \
  GAUGE(send_message_v2_active, Accumulate)                                                        \
  GAUGE(pop_message_active, Accumulate)                                                            \
  GAUGE(get_topic_route_active, Accumulate)                                                        \
  GAUGE(send_message_pending, Accumulate)                                                          \
  GAUGE(pop_message_pending, Accumulate)                                                           \
  GAUGE(get_topic_route_pending, Accumulate)                                                       \
  GAUGE(total_pending, Accumulate)                                                                 \
  HISTOGRAM(request_time_ms, Milliseconds)

/**
 * Struct definition for all rocketmq proxy stats. @see stats_macros.h
 */
struct RocketmqFilterStats {
  ALL_ROCKETMQ_FILTER_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT,
                            GENERATE_HISTOGRAM_STRUCT)

  static RocketmqFilterStats generateStats(const std::string& prefix, Stats::Scope& scope) {
    return RocketmqFilterStats{ALL_ROCKETMQ_FILTER_STATS(POOL_COUNTER_PREFIX(scope, prefix),
                                                         POOL_GAUGE_PREFIX(scope, prefix),
                                                         POOL_HISTOGRAM_PREFIX(scope, prefix))};
  }
};

} // namespace RocketmqProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
