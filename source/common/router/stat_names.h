#include "envoy/stats/stats_macros.h"

#pragma once

namespace Envoy {
namespace Router {

/**
 * All router filter stats. @see stats_macros.h
 */
#define ALL_ROUTER_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                        \
  COUNTER(no_cluster)                                                                              \
  COUNTER(no_route)                                                                                \
  COUNTER(passthrough_internal_redirect_bad_location)                                              \
  COUNTER(passthrough_internal_redirect_no_route)                                                  \
  COUNTER(passthrough_internal_redirect_predicate)                                                 \
  COUNTER(passthrough_internal_redirect_too_many_redirects)                                        \
  COUNTER(passthrough_internal_redirect_unsafe_scheme)                                             \
  COUNTER(rq_direct_response)                                                                      \
  COUNTER(rq_redirect)                                                                             \
  COUNTER(rq_reset_after_downstream_response_started)                                              \
  COUNTER(rq_total)                                                                                \
  STATNAME(retry)

MAKE_STAT_NAMES_STRUCT(RouterStatNames, ALL_ROUTER_STATS);

} // namespace Router
} // namespace Envoy
