#pragma once

#include <string>
#include <vector>

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/router/router.h"
#include "envoy/stats/stats_macros.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

#define ALL_SET_METADATA_FILTER_STATS(COUNTER) COUNTER(overwrite_denied)

struct FilterStats {
  ALL_SET_METADATA_FILTER_STATS(GENERATE_COUNTER_STRUCT)
};

struct UntypedMetadataEntry {
  bool allow_overwrite{};
  std::string metadata_namespace;
  Protobuf::Struct value;
};
struct TypedMetadataEntry {
  bool allow_overwrite{};
  std::string metadata_namespace;
  Protobuf::Any value;
};
class Config : public Envoy::Router::RouteSpecificFilterConfig,
               public Logger::Loggable<Logger::Id::config> {
public:
  Config(const envoy::extensions::filters::http::set_metadata::v3::Config& config,
         Stats::Scope& scope, const std::string& stats_prefix);

  const std::vector<UntypedMetadataEntry>& untyped() { return untyped_; }
  const std::vector<TypedMetadataEntry>& typed() { return typed_; }
  const FilterStats& stats() const { return stats_; }

private:
  static FilterStats generateStats(const std::string& prefix, Stats::Scope& scope);

  std::vector<UntypedMetadataEntry> untyped_;
  std::vector<TypedMetadataEntry> typed_;
  FilterStats stats_;
};

using ConfigSharedPtr = std::shared_ptr<Config>;

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
