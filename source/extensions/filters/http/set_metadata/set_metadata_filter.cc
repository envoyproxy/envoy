#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "source/common/config/well_known_names.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

namespace {

void applyConfigToMetadata(const Config& config,
                           envoy::config::core::v3::Metadata& dynamic_metadata) {

  // Add configured untyped metadata.
  if (!config.untyped().empty()) {
    auto& mut_untyped_metadata = *dynamic_metadata.mutable_filter_metadata();

    for (const auto& entry : config.untyped()) {
      if (!mut_untyped_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_untyped_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // Get the existing metadata at this key for merging.
        Protobuf::Struct& orig_fields = mut_untyped_metadata[entry.metadata_namespace];
        const auto& to_merge = entry.value;

        // Merge the new metadata into the existing metadata.
        StructUtil::update(orig_fields, to_merge);
      } else {
        // The entry exists, and we are not allowed to overwrite -- emit a stat.
        config.stats().overwrite_denied_.inc();
      }
    }
  }

  // Add configured typed metadata.
  if (!config.typed().empty()) {
    auto& mut_typed_metadata = *dynamic_metadata.mutable_typed_filter_metadata();

    for (const auto& entry : config.typed()) {
      if (!mut_typed_metadata.contains(entry.metadata_namespace)) {
        // Insert the new entry.
        mut_typed_metadata[entry.metadata_namespace] = entry.value;
      } else if (entry.allow_overwrite) {
        // Overwrite the existing typed metadata at this key.
        mut_typed_metadata[entry.metadata_namespace] = entry.value;
      } else {
        // The entry exists, and we are not allowed to overwrite -- emit a stat.
        config.stats().overwrite_denied_.inc();
      }
    }
  }
}

} // namespace

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

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // Apply global config first.
  applyConfigToMetadata(*config_, decoder_callbacks_->streamInfo().dynamicMetadata());

  // Apply route-specific config if present.
  const Config* route_specific_cfg =
      dynamic_cast<const Config*>(decoder_callbacks_->mostSpecificPerFilterConfig());
  if (route_specific_cfg) {
    applyConfigToMetadata(*route_specific_cfg, decoder_callbacks_->streamInfo().dynamicMetadata());
  }

  return Http::FilterHeadersStatus::Continue;
}

Http::FilterDataStatus SetMetadataFilter::decodeData(Buffer::Instance&, bool) {
  return Http::FilterDataStatus::Continue;
}

void SetMetadataFilter::setDecoderFilterCallbacks(Http::StreamDecoderFilterCallbacks& callbacks) {
  decoder_callbacks_ = &callbacks;
}

} // namespace SetMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
