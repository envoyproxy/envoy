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
  value_.emplace<ProtobufWkt::Struct&>(proto_config.value());

  if (proto_config.has_untyped_metadata()) {
    value_.emplace<ProtobufWkt::Struct&>(proto_config.untyped_metadata().value());
  } else if (proto_config.has_typed_metadata()) {
    value_.emplace<ProtobufWkt::Any&>(proto_config.typed_metadata().value());
  }
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const absl::string_view metadata_namespace = config_->metadataNamespace();

  if (absl::holds_alternative<ProtobufWkt::Struct>(config_->value())) {
  auto& metadata = *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();
  // get the existing metadata at this key for merging
  ProtobufWkt::Struct& org_fields =
      metadata[toStdStringView(metadata_namespace)]; // NOLINT(std::string_view)
  const auto& to_merge = absl::get<const ProtobufWkt::Struct>(config_->value());

  // merge the new metadata into the existing metadata
  StructUtil::update(org_fields, to_merge);
  } else if (absl::holds_alternative<ProtobufWkt::Any>(config_->value())) {
    auto& metadata = *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_typed_filter_metadata();
    // overwrite the existing typed metadata at this key
    metadata[metadata_namespace] = absl::get<const ProtobufWkt::Any>(config_->value());
  } else {
    ENVOY_LOG(error, "variant holds no valid alternative value");
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
