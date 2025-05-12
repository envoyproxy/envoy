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
 *
 * There are also cache_hit_ and cache_miss_, defined separately to accommodate extra tags;
 * these two both go into the stat with key `event`, and with tag `event_type=(hit|miss)`
 **/

#define ALL_CACHE_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                         \
  COUNTER(eviction_runs)                                                                           \
  GAUGE(size_bytes, NeverImport)                                                                   \
  GAUGE(size_count, NeverImport)                                                                   \
  GAUGE(size_limit_bytes, NeverImport)                                                             \
  GAUGE(size_limit_count, NeverImport)                                                             \
  STATNAME(cache)                                                                                  \
  STATNAME(cache_path)                                                                             \
  STATNAME(event)                                                                                  \
  STATNAME(event_type)                                                                             \
  STATNAME(hit)                                                                                    \
  STATNAME(miss)
// TODO(ravenblack): Add other stats from DESIGN.md

#define COUNTER_HELPER_(NAME)                                                                      \
  , NAME##_(                                                                                       \
        Envoy::Stats::Utility::counterFromStatNames(scope, {prefix_, stat_names.NAME##_}, tags_))
#define GAUGE_HELPER_(NAME, MODE)                                                                  \
  , NAME##_(Envoy::Stats::Utility::gaugeFromStatNames(                                             \
        scope, {prefix_, stat_names.NAME##_}, Envoy::Stats::Gauge::ImportMode::MODE, tags_))
#define STATNAME_HELPER_(NAME)

MAKE_STAT_NAMES_STRUCT(CacheStatNames, ALL_CACHE_STATS);

struct CacheStats {
  CacheStats(const CacheStatNames& stat_names, Envoy::Stats::Scope& scope,
             Stats::StatName cache_path)
      : stat_names_(stat_names), prefix_(stat_names_.cache_), cache_path_(cache_path),
        tags_({{stat_names_.cache_path_, cache_path_}}),
        tags_hit_(
            {{stat_names_.cache_path_, cache_path_}, {stat_names_.event_type_, stat_names_.hit_}}),
        tags_miss_(
            {{stat_names_.cache_path_, cache_path_}, {stat_names_.event_type_, stat_names_.miss_}})
            ALL_CACHE_STATS(COUNTER_HELPER_, GAUGE_HELPER_, HISTOGRAM_HELPER_, TEXT_READOUT_HELPER_,
                            STATNAME_HELPER_),
        cache_hit_(Envoy::Stats::Utility::counterFromStatNames(scope, {prefix_, stat_names.event_},
                                                               tags_hit_)),
        cache_miss_(Envoy::Stats::Utility::counterFromStatNames(scope, {prefix_, stat_names.event_},
                                                                tags_miss_)) {}

private:
  const CacheStatNames& stat_names_;
  const Stats::StatName prefix_;
  const Stats::StatName cache_path_;
  Stats::StatNameTagVector tags_;
  Stats::StatNameTagVector tags_hit_;
  Stats::StatNameTagVector tags_miss_;

public:
  ALL_CACHE_STATS(GENERATE_COUNTER_STRUCT, GENERATE_GAUGE_STRUCT, GENERATE_HISTOGRAM_STRUCT,
                  GENERATE_TEXT_READOUT_STRUCT, GENERATE_STATNAME_STRUCT);
  Stats::Counter& cache_hit_;
  Stats::Counter& cache_miss_;
};

CacheStats generateStats(CacheStatNames& stat_names, Stats::Scope& scope,
                         absl::string_view cache_path);

} // namespace FileSystemHttpCache
} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
