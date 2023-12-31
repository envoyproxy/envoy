#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Upstream {

/**
 * All cluster config update related stats.
 * See https://github.com/envoyproxy/envoy/issues/23575 for details. Stats from ClusterInfo::stats()
 * will be split into subgroups "config-update", "lb", "endpoint" and "the rest"(which are mainly
 * upstream related), roughly based on their semantics.
 */
#define ALL_CLUSTER_CONFIG_UPDATE_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)         \
  COUNTER(assignment_stale)                                                                        \
  COUNTER(assignment_timeout_received)                                                             \
  COUNTER(assignment_use_cached)                                                                   \
  COUNTER(update_attempt)                                                                          \
  COUNTER(update_empty)                                                                            \
  COUNTER(update_failure)                                                                          \
  COUNTER(update_no_rebuild)                                                                       \
  COUNTER(update_success)                                                                          \
  GAUGE(version, NeverImport)                                                                      \
  GAUGE(warming_state, NeverImport)

/**
 * All cluster endpoints related stats.
 */
#define ALL_CLUSTER_ENDPOINT_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)              \
  GAUGE(max_host_weight, NeverImport)                                                              \
  COUNTER(membership_change)                                                                       \
  GAUGE(membership_degraded, NeverImport)                                                          \
  GAUGE(membership_excluded, NeverImport)                                                          \
  GAUGE(membership_healthy, NeverImport)                                                           \
  GAUGE(membership_total, NeverImport)

/**
 * All cluster load balancing related stats.
 */
#define ALL_CLUSTER_LB_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                    \
  COUNTER(lb_healthy_panic)                                                                        \
  COUNTER(lb_local_cluster_not_ok)                                                                 \
  COUNTER(lb_recalculate_zone_structures)                                                          \
  COUNTER(lb_subsets_created)                                                                      \
  COUNTER(lb_subsets_fallback)                                                                     \
  COUNTER(lb_subsets_fallback_panic)                                                               \
  COUNTER(lb_subsets_removed)                                                                      \
  COUNTER(lb_subsets_selected)                                                                     \
  COUNTER(lb_zone_cluster_too_small)                                                               \
  COUNTER(lb_zone_no_capacity_left)                                                                \
  COUNTER(lb_zone_number_differs)                                                                  \
  COUNTER(lb_zone_routing_all_directly)                                                            \
  COUNTER(lb_zone_routing_cross_zone)                                                              \
  COUNTER(lb_zone_routing_sampled)                                                                 \
  GAUGE(lb_subsets_active, Accumulate)

/**
 * All cluster stats. @see stats_macros.h
 */
#define ALL_CLUSTER_TRAFFIC_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)               \
  COUNTER(bind_errors)                                                                             \
  COUNTER(original_dst_host_invalid)                                                               \
  COUNTER(retry_or_shadow_abandoned)                                                               \
  COUNTER(upstream_cx_close_notify)                                                                \
  COUNTER(upstream_cx_connect_attempts_exceeded)                                                   \
  COUNTER(upstream_cx_connect_fail)                                                                \
  COUNTER(upstream_cx_connect_timeout)                                                             \
  COUNTER(upstream_cx_connect_with_0_rtt)                                                          \
  COUNTER(upstream_cx_destroy)                                                                     \
  COUNTER(upstream_cx_destroy_local)                                                               \
  COUNTER(upstream_cx_destroy_local_with_active_rq)                                                \
  COUNTER(upstream_cx_destroy_remote)                                                              \
  COUNTER(upstream_cx_destroy_remote_with_active_rq)                                               \
  COUNTER(upstream_cx_destroy_with_active_rq)                                                      \
  COUNTER(upstream_cx_http1_total)                                                                 \
  COUNTER(upstream_cx_http2_total)                                                                 \
  COUNTER(upstream_cx_http3_total)                                                                 \
  COUNTER(upstream_cx_idle_timeout)                                                                \
  COUNTER(upstream_cx_max_duration_reached)                                                        \
  COUNTER(upstream_cx_max_requests)                                                                \
  COUNTER(upstream_cx_none_healthy)                                                                \
  COUNTER(upstream_cx_overflow)                                                                    \
  COUNTER(upstream_cx_pool_overflow)                                                               \
  COUNTER(upstream_cx_protocol_error)                                                              \
  COUNTER(upstream_cx_rx_bytes_total)                                                              \
  COUNTER(upstream_cx_total)                                                                       \
  COUNTER(upstream_cx_tx_bytes_total)                                                              \
  COUNTER(upstream_flow_control_backed_up_total)                                                   \
  COUNTER(upstream_flow_control_drained_total)                                                     \
  COUNTER(upstream_flow_control_paused_reading_total)                                              \
  COUNTER(upstream_flow_control_resumed_reading_total)                                             \
  COUNTER(upstream_internal_redirect_failed_total)                                                 \
  COUNTER(upstream_internal_redirect_succeeded_total)                                              \
  COUNTER(upstream_rq_cancelled)                                                                   \
  COUNTER(upstream_rq_completed)                                                                   \
  COUNTER(upstream_rq_maintenance_mode)                                                            \
  COUNTER(upstream_rq_max_duration_reached)                                                        \
  COUNTER(upstream_rq_pending_failure_eject)                                                       \
  COUNTER(upstream_rq_pending_overflow)                                                            \
  COUNTER(upstream_rq_pending_total)                                                               \
  COUNTER(upstream_rq_0rtt)                                                                        \
  COUNTER(upstream_rq_per_try_timeout)                                                             \
  COUNTER(upstream_rq_per_try_idle_timeout)                                                        \
  COUNTER(upstream_rq_retry)                                                                       \
  COUNTER(upstream_rq_retry_backoff_exponential)                                                   \
  COUNTER(upstream_rq_retry_backoff_ratelimited)                                                   \
  COUNTER(upstream_rq_retry_limit_exceeded)                                                        \
  COUNTER(upstream_rq_retry_overflow)                                                              \
  COUNTER(upstream_rq_retry_success)                                                               \
  COUNTER(upstream_rq_rx_reset)                                                                    \
  COUNTER(upstream_rq_timeout)                                                                     \
  COUNTER(upstream_rq_total)                                                                       \
  COUNTER(upstream_rq_tx_reset)                                                                    \
  COUNTER(upstream_http3_broken)                                                                   \
  GAUGE(upstream_cx_active, Accumulate)                                                            \
  GAUGE(upstream_cx_rx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_cx_tx_bytes_buffered, Accumulate)                                                 \
  GAUGE(upstream_rq_active, Accumulate)                                                            \
  GAUGE(upstream_rq_pending_active, Accumulate)                                                    \
  HISTOGRAM(upstream_cx_connect_ms, Milliseconds)                                                  \
  HISTOGRAM(upstream_cx_length_ms, Milliseconds)

/**
 * All cluster load report stats. These are only use for EDS load reporting and not sent to the
 * stats sink. See envoy.config.endpoint.v3.ClusterStats for the definition of
 * total_dropped_requests and dropped_requests, which correspond to the upstream_rq_dropped and
 * upstream_rq_drop_overload counter here. These are latched by LoadStatsReporter, independent of
 * the normal stats sink flushing.
 */
#define ALL_CLUSTER_LOAD_REPORT_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)           \
  COUNTER(upstream_rq_dropped)                                                                     \
  COUNTER(upstream_rq_drop_overload)

/**
 * Cluster circuit breakers gauges. Note that we do not generate a stats
 * structure from this macro. This is because depending on flags, we want to use
 * null gauges for all the "remaining" ones. This is hard to automate with the
 * 2-phase macros, so ClusterInfoImpl::generateCircuitBreakersStats is
 * hand-coded and must be changed if we alter the set of gauges in this macro.
 * We also include stat-names in this structure that are used when composing
 * the circuit breaker names, depending on priority settings.
 */
#define ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)      \
  GAUGE(cx_open, Accumulate)                                                                       \
  GAUGE(cx_pool_open, Accumulate)                                                                  \
  GAUGE(rq_open, Accumulate)                                                                       \
  GAUGE(rq_pending_open, Accumulate)                                                               \
  GAUGE(rq_retry_open, Accumulate)                                                                 \
  GAUGE(remaining_cx, Accumulate)                                                                  \
  GAUGE(remaining_cx_pools, Accumulate)                                                            \
  GAUGE(remaining_pending, Accumulate)                                                             \
  GAUGE(remaining_retries, Accumulate)                                                             \
  GAUGE(remaining_rq, Accumulate)                                                                  \
  STATNAME(circuit_breakers)                                                                       \
  STATNAME(default)                                                                                \
  STATNAME(high)

/**
 * All stats tracking request/response headers and body sizes. Not used by default.
 */
#define ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME) \
  HISTOGRAM(upstream_rq_headers_size, Bytes)                                                       \
  HISTOGRAM(upstream_rq_body_size, Bytes)                                                          \
  HISTOGRAM(upstream_rs_headers_size, Bytes)                                                       \
  HISTOGRAM(upstream_rs_body_size, Bytes)

/**
 * All stats around timeout budgets. Not used by default.
 */
#define ALL_CLUSTER_TIMEOUT_BUDGET_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)        \
  HISTOGRAM(upstream_rq_timeout_budget_percent_used, Unspecified)                                  \
  HISTOGRAM(upstream_rq_timeout_budget_per_try_percent_used, Unspecified)

/**
 * Struct definition for cluster config update stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(ClusterConfigUpdateStatNames, ALL_CLUSTER_CONFIG_UPDATE_STATS);
MAKE_STATS_STRUCT(ClusterConfigUpdateStats, ClusterConfigUpdateStatNames,
                  ALL_CLUSTER_CONFIG_UPDATE_STATS);

/**
 * Struct definition for cluster endpoint related stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(ClusterEndpointStatNames, ALL_CLUSTER_ENDPOINT_STATS);
MAKE_STATS_STRUCT(ClusterEndpointStats, ClusterEndpointStatNames, ALL_CLUSTER_ENDPOINT_STATS);

/**
 * Struct definition for cluster load balancing stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(ClusterLbStatNames, ALL_CLUSTER_LB_STATS);
MAKE_STATS_STRUCT(ClusterLbStats, ClusterLbStatNames, ALL_CLUSTER_LB_STATS);

/**
 * Struct definition for all cluster traffic stats. @see stats_macros.h
 */
MAKE_STAT_NAMES_STRUCT(ClusterTrafficStatNames, ALL_CLUSTER_TRAFFIC_STATS);
MAKE_STATS_STRUCT(ClusterTrafficStats, ClusterTrafficStatNames, ALL_CLUSTER_TRAFFIC_STATS);
using DeferredCreationCompatibleClusterTrafficStats =
    Stats::DeferredCreationCompatibleStats<ClusterTrafficStats>;

MAKE_STAT_NAMES_STRUCT(ClusterLoadReportStatNames, ALL_CLUSTER_LOAD_REPORT_STATS);
MAKE_STATS_STRUCT(ClusterLoadReportStats, ClusterLoadReportStatNames,
                  ALL_CLUSTER_LOAD_REPORT_STATS);

// We can't use macros to make the Stats class for circuit breakers due to
// the conditional inclusion of 'remaining' gauges. But we do auto-generate
// the StatNames struct.
MAKE_STAT_NAMES_STRUCT(ClusterCircuitBreakersStatNames, ALL_CLUSTER_CIRCUIT_BREAKERS_STATS);

MAKE_STAT_NAMES_STRUCT(ClusterRequestResponseSizeStatNames,
                       ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS);
MAKE_STATS_STRUCT(ClusterRequestResponseSizeStats, ClusterRequestResponseSizeStatNames,
                  ALL_CLUSTER_REQUEST_RESPONSE_SIZE_STATS);

MAKE_STAT_NAMES_STRUCT(ClusterTimeoutBudgetStatNames, ALL_CLUSTER_TIMEOUT_BUDGET_STATS);
MAKE_STATS_STRUCT(ClusterTimeoutBudgetStats, ClusterTimeoutBudgetStatNames,
                  ALL_CLUSTER_TIMEOUT_BUDGET_STATS);

/**
 * Struct definition for cluster circuit breakers stats. @see stats_macros.h
 */
struct ClusterCircuitBreakersStats {
  ALL_CLUSTER_CIRCUIT_BREAKERS_STATS(c, GENERATE_GAUGE_STRUCT, h, tr, GENERATE_STATNAME_STRUCT)
};

using ClusterRequestResponseSizeStatsPtr = std::unique_ptr<ClusterRequestResponseSizeStats>;
using ClusterRequestResponseSizeStatsOptRef =
    absl::optional<std::reference_wrapper<ClusterRequestResponseSizeStats>>;

using ClusterTimeoutBudgetStatsPtr = std::unique_ptr<ClusterTimeoutBudgetStats>;
using ClusterTimeoutBudgetStatsOptRef =
    absl::optional<std::reference_wrapper<ClusterTimeoutBudgetStats>>;

} // namespace Upstream
} // namespace Envoy
