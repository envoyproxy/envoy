#include "source/extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"
#include "envoy/stream_info/filter_state.h"

#include "source/common/config/well_known_names.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

namespace {

void setTypedMetadata(const Config& config, envoy::config::core::v3::Metadata& dynamic_metadata,
                      const std::string& metadata_namespace, bool allow_overwrite,
                      const Protobuf::Any& value) {
  auto& mut_typed_metadata = *dynamic_metadata.mutable_typed_filter_metadata();
  if (!mut_typed_metadata.contains(metadata_namespace) || allow_overwrite) {
    // Insert the new entry, or overwrite the existing typed metadata at this key.
    mut_typed_metadata[metadata_namespace] = value;
  } else {
    // The entry exists, and we are not allowed to overwrite -- emit a stat.
    config.stats().overwrite_denied_.inc();
  }
}

// Serializes a filter state object into an Any for typed metadata. If `value_is_serialized_any`,
// the object's string serialization is parsed as an Any. Returns false if the object can't be
// serialized this way.
bool serializeFilterStateObject(const StreamInfo::FilterState::Object& object,
                                bool value_is_serialized_any, Protobuf::Any& out) {
  if (value_is_serialized_any) {
    // The object's string serialization is a serialized Any.
    std::optional<std::string> str = object.serializeAsString();
    return str.has_value() && out.ParseFromString(*str);
  }
  if (ProtobufTypes::MessagePtr proto = object.serializeAsProto(); proto != nullptr) {
    if (proto->GetTypeName() == Protobuf::Any::descriptor()->full_name()) {
      // The object already serializes to an Any; use it directly rather than double-wrapping.
      out = *Protobuf::DynamicCastMessage<Protobuf::Any>(proto.get());
    } else {
      out.PackFrom(*proto);
    }
    return true;
  }
  if (std::optional<std::string> str = object.serializeAsString(); str.has_value()) {
    Protobuf::StringValue string_value;
    string_value.set_value(std::move(*str));
    out.PackFrom(string_value);
    return true;
  }
  return false;
}

void applyConfigToMetadata(const Config& config, const StreamInfo::FilterState& filter_state,
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

  // Add typed metadata copied from filter state objects.
  for (const auto& entry : config.filterState()) {
    const auto* object = filter_state.getDataReadOnly<StreamInfo::FilterState::Object>(entry.key);
    if (object == nullptr) {
      config.stats().filter_state_not_found_.inc();
      continue;
    }
    Protobuf::Any value;
    if (!serializeFilterStateObject(*object, entry.value_is_serialized_any, value)) {
      config.stats().filter_state_not_serializable_.inc();
      continue;
    }
    setTypedMetadata(config, dynamic_metadata, entry.metadata_namespace, entry.allow_overwrite,
                     value);
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
    } else if (metadata.has_filter_state()) {
      FilterStateMetadataEntry filter_state_entry{
          metadata.allow_overwrite(), metadata.metadata_namespace(), metadata.filter_state().key(),
          metadata.filter_state().value_is_serialized_any()};
      filter_state_.emplace_back(filter_state_entry);
    } else {
      ENVOY_LOG(warn, "set_metadata filter configuration contains metadata entries without value, "
                      "typed_value or filter_state");
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
  StreamInfo::StreamInfo& stream_info = decoder_callbacks_->streamInfo();
  const StreamInfo::FilterState& filter_state = *stream_info.filterState();

  // Apply global config first.
  applyConfigToMetadata(*config_, filter_state, stream_info.dynamicMetadata());

  // Apply route-specific config if present.
  const Config* route_specific_cfg =
      dynamic_cast<const Config*>(decoder_callbacks_->mostSpecificPerFilterConfig());
  if (route_specific_cfg) {
    applyConfigToMetadata(*route_specific_cfg, filter_state, stream_info.dynamicMetadata());
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
