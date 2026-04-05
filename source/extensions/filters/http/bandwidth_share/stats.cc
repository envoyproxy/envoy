#include "source/extensions/filters/http/bandwidth_share/stats.h"

#include "source/common/stats/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {

BandwidthShareStats::BandwidthShareStats(const BandwidthShareStatNames& stat_names,
                                         Stats::Scope& scope, absl::string_view bucket_id,
                                         absl::string_view tenant, bool is_response)
    : dynamic_pool_(scope.symbolTable()), stat_names_(stat_names),
      bucket_id_(dynamic_pool_.add(bucket_id)), tenant_(dynamic_pool_.add(tenant)),
      request_or_response_(is_response ? stat_names_.response_ : stat_names_.request_),
      tags_gauge_({{stat_names_.bucket_id_, bucket_id_},
                   {stat_names_.tenant_, tenant_},
                   {stat_names_.direction_, request_or_response_}}),
      tags_limited_({{stat_names_.bucket_id_, bucket_id_},
                     {stat_names_.tenant_, tenant_},
                     {stat_names_.direction_, request_or_response_},
                     {stat_names_.handling_, stat_names_.limited_}}),
      tags_not_limited_({{stat_names_.bucket_id_, bucket_id_},
                         {stat_names_.tenant_, tenant_},
                         {stat_names_.direction_, request_or_response_},
                         {stat_names_.handling_, stat_names_.not_limited_}}),
      bytes_not_limited_(Stats::Utility::counterFromStatNames(
          scope, {stat_names_.bandwidth_share_, stat_names_.bytes_}, tags_not_limited_)),
      bytes_limited_(Stats::Utility::counterFromStatNames(
          scope, {stat_names_.bandwidth_share_, stat_names_.bytes_}, tags_limited_)),
      streams_currently_limited_(Stats::Utility::gaugeFromStatNames(
          scope, {stat_names_.bandwidth_share_, stat_names_.streams_currently_limited_},
          Stats::Gauge::ImportMode::Accumulate, tags_gauge_)),
      bytes_pending_(Stats::Utility::gaugeFromStatNames(
          scope, {stat_names_.bandwidth_share_, stat_names_.bytes_pending_},
          Stats::Gauge::ImportMode::Accumulate, tags_gauge_)) {}

} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
