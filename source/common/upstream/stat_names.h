#include "envoy/stats/stats_macros.h"

#pragma once

namespace Envoy {
namespace Upstream {

/**
 * All cluster manager stats. @see stats_macros.h
 */
#define ALL_CLUSTER_MANAGER_STATS(COUNTER, GAUGE, STATNAME)                                        \
  COUNTER(cluster_added)                                                                           \
  COUNTER(cluster_modified)                                                                        \
  COUNTER(cluster_removed)                                                                         \
  COUNTER(cluster_updated)                                                                         \
  COUNTER(cluster_updated_via_merge)                                                               \
  COUNTER(update_merge_cancelled)                                                                  \
  COUNTER(update_out_of_merge_window)                                                              \
  GAUGE(active_clusters, NeverImport)                                                              \
  GAUGE(warming_clusters, NeverImport)                                                             \
  STATNAME(cluster_manager)

MAKE_STAT_NAMES_STRUCT(ClusterManagerStatNames, ALL_CLUSTER_MANAGER_STATS);

} // namespace Upstream
} // namespace Envoy
