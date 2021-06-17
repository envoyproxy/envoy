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
  value_ = proto_config.value();
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const absl::string_view metadata_namespace = config_->metadataNamespace();
  auto& metadata = *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();
  ProtobufWkt::Struct& org_fields =
      metadata[toStdStringView(metadata_namespace)]; // NOLINT(std::string_view)
  const ProtobufWkt::Struct& to_merge = config_->value();

  StructUtil::update(org_fields, to_merge);

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
