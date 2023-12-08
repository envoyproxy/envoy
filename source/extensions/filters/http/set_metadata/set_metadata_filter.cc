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

Config::Config(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config) {
  namespace_ = proto_config.metadata_namespace();
  untyped_value_ = proto_config.value();

  if (proto_config.has_untyped_metadata()) {
    allow_overwrite_ = proto_config.untyped_metadata().allow_overwrite();
    untyped_value_ = proto_config.untyped_metadata().value();
  } else if (proto_config.has_typed_metadata()) {
    has_untyped_value_ = false;
    has_typed_value_ = true;
    allow_overwrite_ = proto_config.typed_metadata().allow_overwrite();
    typed_value_ = proto_config.typed_metadata().value();
  }
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const absl::string_view metadata_namespace = config_->metadataNamespace();

  if (config_->hasUntypedValue()) {
    auto& metadata = *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();

    if (!metadata.contains(metadata_namespace)) {
      metadata[metadata_namespace] = config_->untypedValue();
    } else if (config_->allowOverwrite()) {
      // get the existing metadata at this key for merging
      ProtobufWkt::Struct& orig_fields = metadata[metadata_namespace];
      const auto& to_merge = config_->untypedValue();

      // merge the new metadata into the existing metadata
      StructUtil::update(orig_fields, to_merge);
    }
  } else if (config_->hasTypedValue()) {
    auto& metadata =
        *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_typed_filter_metadata();
    // overwrite the existing typed metadata at this key
    metadata[metadata_namespace] = config_->typedValue();
  } else {
    ENVOY_LOG(error, "no configured metadata found");
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
