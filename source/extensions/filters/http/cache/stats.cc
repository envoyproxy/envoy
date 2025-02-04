#include "source/extensions/filters/http/cache/stats.h"

#include "envoy/stats/stats_macros.h"

#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

#define CACHE_FILTER_STATS(COUNTER, GAUGE, HISTOGRAM, TEXT_READOUT, STATNAME)                      \
  STATNAME(cache)                                                                                  \
  STATNAME(cache_label)                                                                            \
  STATNAME(event)                                                                                  \
  STATNAME(event_type)                                                                             \
  STATNAME(hit)                                                                                    \
  STATNAME(miss)                                                                                   \
  STATNAME(uncacheable)                                                                            \
  STATNAME(upstream_reset)                                                                         \
  STATNAME(lookup_error)                                                                           \
  STATNAME(validate)

MAKE_STAT_NAMES_STRUCT(CacheStatNames, CACHE_FILTER_STATS);

using Envoy::Stats::Utility::counterFromStatNames;

class CacheFilterStatsImpl : public CacheFilterStats {
public:
  CacheFilterStatsImpl(Stats::Scope& scope, absl::string_view label)
      : stat_names_(scope.symbolTable()), prefix_(stat_names_.cache_),
        label_(stat_names_.pool_.add(absl::StrReplaceAll(label, {{".", "_"}}))),
        tags_hit_(
            {{stat_names_.cache_label_, label_}, {stat_names_.event_type_, stat_names_.hit_}}),
        tags_miss_(
            {{stat_names_.cache_label_, label_}, {stat_names_.event_type_, stat_names_.miss_}}),
        tags_uncacheable_({{stat_names_.cache_label_, label_},
                           {stat_names_.event_type_, stat_names_.uncacheable_}}),
        tags_upstream_reset_({{stat_names_.cache_label_, label_},
                              {stat_names_.event_type_, stat_names_.upstream_reset_}}),
        tags_lookup_error_({{stat_names_.cache_label_, label_},
                            {stat_names_.event_type_, stat_names_.lookup_error_}}),
        tags_validate_(
            {{stat_names_.cache_label_, label_}, {stat_names_.event_type_, stat_names_.validate_}}),
        counter_hit_(counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_hit_)),
        counter_miss_(counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_miss_)),
        counter_uncacheable_(
            counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_uncacheable_)),
        counter_upstream_reset_(
            counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_upstream_reset_)),
        counter_lookup_error_(
            counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_lookup_error_)),
        counter_validate_(
            counterFromStatNames(scope, {prefix_, stat_names_.event_}, tags_validate_)) {}
  void incForStatus(CacheEntryStatus status) override;
  CacheStatNames stat_names_;
  const Stats::StatName prefix_;
  const Stats::StatName label_;
  const Stats::StatNameTagVector tags_hit_;
  const Stats::StatNameTagVector tags_miss_;
  const Stats::StatNameTagVector tags_uncacheable_;
  const Stats::StatNameTagVector tags_upstream_reset_;
  const Stats::StatNameTagVector tags_lookup_error_;
  const Stats::StatNameTagVector tags_validate_;
  Stats::Counter& counter_hit_;
  Stats::Counter& counter_miss_;
  Stats::Counter& counter_uncacheable_;
  Stats::Counter& counter_upstream_reset_;
  Stats::Counter& counter_lookup_error_;
  Stats::Counter& counter_validate_;
};

CacheFilterStatsPtr generateStats(Stats::Scope& scope, absl::string_view label) {
  return std::make_unique<CacheFilterStatsImpl>(scope, label);
}

void CacheFilterStatsImpl::incForStatus(CacheEntryStatus status) {
  switch (status) {
  case CacheEntryStatus::Miss:
  case CacheEntryStatus::FailedValidation:
    return counter_miss_.inc();
  case CacheEntryStatus::Hit:
  case CacheEntryStatus::FoundNotModified:
  case CacheEntryStatus::Streamed:
  case CacheEntryStatus::ValidatedFree:
    return counter_hit_.inc();
  case CacheEntryStatus::Validated:
    return counter_validate_.inc();
  case CacheEntryStatus::UpstreamReset:
    return counter_upstream_reset_.inc();
  case CacheEntryStatus::Uncacheable:
    return counter_uncacheable_.inc();
  case CacheEntryStatus::LookupError:
    return counter_lookup_error_.inc();
  }
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
