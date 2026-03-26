#pragma once

#include "envoy/stats/scope.h"
#include "envoy/stats/tag.h"

#include "source/common/stats/symbol_table.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

/**
 * All BandwidthShare stats.
 *
 * Not using stats macros due to using tags that macros don't support well.
 *
 * Stats-groups may be periodically purged if unused, to prevent accumulation
 * of unlimited tag names.
 **/

struct BandwidthShareStatNames {
  explicit BandwidthShareStatNames(Stats::Scope& scope)
      : pool_(scope.symbolTable()), bytes_(pool_.add("bytes")), handling_(pool_.add("handling")),
        limited_(pool_.add("limited")), not_limited_(pool_.add("not_limited")),
        streams_currently_limited_(pool_.add("streams_currently_limited")),
        bytes_pending_(pool_.add("bytes_pending")), bandwidth_share_(pool_.add("bandwidth_share")),
        direction_(pool_.add("direction")), request_(pool_.add("request")),
        response_(pool_.add("response")), bucket_id_(pool_.add("bucket_id")),
        tenant_(pool_.add("tenant")) {}

  Stats::StatNamePool pool_;
  Stats::StatName bytes_, handling_, limited_, not_limited_, streams_currently_limited_,
      bytes_pending_, bandwidth_share_, direction_, request_, response_, bucket_id_, tenant_;
};

struct BandwidthShareStats {
public:
  BandwidthShareStats(const BandwidthShareStatNames& stat_names, Stats::Scope& scope,
                      absl::string_view bucket_id, absl::string_view tenant, bool is_response);

private:
  Stats::StatNameDynamicPool dynamic_pool_;
  const BandwidthShareStatNames& stat_names_;
  const Stats::StatName bucket_id_;
  const Stats::StatName tenant_;
  const Stats::StatName request_or_response_;
  const Stats::StatNameTagVector tags_gauge_;
  const Stats::StatNameTagVector tags_limited_;
  const Stats::StatNameTagVector tags_not_limited_;

public:
  Stats::Counter& bytes_not_limited_;
  Stats::Counter& bytes_limited_;
  Stats::Gauge& streams_currently_limited_;
  Stats::Gauge& bytes_pending_;
};

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
