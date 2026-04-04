#include "source/extensions/filters/http/set_metadata/filter_config.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

Config::Config(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config,
               Stats::Scope& scope, const std::string& stats_prefix)
    : stats_(generateStats(stats_prefix, scope)) {
  if (proto_config.has_value() && !proto_config.metadata_namespace().empty()) {
    UntypedMetadataEntry deprecated_api_val{true, proto_config.metadata_namespace(),
                                            proto_config.value()};
    untyped_.emplace_back(deprecated_api_val);
  }

  for (const auto& metadata : proto_config.metadata()) {
    if (metadata.has_value()) {
      UntypedMetadataEntry untyped_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                         metadata.value()};
      untyped_.emplace_back(untyped_entry);
    } else if (metadata.has_typed_value()) {
      TypedMetadataEntry typed_entry{metadata.allow_overwrite(), metadata.metadata_namespace(),
                                     metadata.typed_value()};
      typed_.emplace_back(typed_entry);
    } else {
      ENVOY_LOG(warn, "set_metadata filter configuration contains metadata entries without value "
                      "or typed_value");
    }
  }
}

FilterStats Config::generateStats(const std::string& prefix, Stats::Scope& scope) {
  std::string final_prefix = prefix + "set_metadata.";
  return {ALL_SET_METADATA_FILTER_STATS(POOL_COUNTER_PREFIX(scope, final_prefix))};
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
