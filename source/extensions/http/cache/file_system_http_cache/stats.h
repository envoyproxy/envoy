#pragma once

#include "envoy/stats/stats_macros.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {
namespace FileSystemHttpCache {

/**
 * All cache stats. @see stats_macros.h
 *
 * Note that size_bytes and size_count may drift away from true values, due to:
 * - Changes to the filesystem may be made outside of the process, which will not be
 *   accounted for. (Including, during hot restart, overlapping envoy processes.)
 * - Files completed while pre-cache-purge measurement is in progress may not be counted.
 * - Changes in file size due to header updates are assumed to be negligible, and are ignored.
 *
 * Drift will eventually be reconciled at the next pre-cache-purge measurement.
 **/
#define ALL_CACHE_STATS(COUNTER, GAUGE)                                                            \
  COUNTER(eviction_runs)                                                                           \
  GAUGE(size_bytes, NeverImport)                                                                   \
  GAUGE(size_count, NeverImport)                                                                   \
  GAUGE(size_limit_bytes, NeverImport)                                                             \
  GAUGE(size_limit_count, NeverImport)
// TODO(ravenblack): Add other stats from DESIGN.md

struct CacheStats {
  ALL_CACHE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT)
};

CacheStats generateStats(Stats::Scope& scope, absl::string_view cache_path);

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
