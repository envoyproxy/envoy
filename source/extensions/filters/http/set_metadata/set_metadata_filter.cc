#include "extensions/filters/http/set_metadata/set_metadata_filter.h"

#include "envoy/extensions/filters/http/set_metadata/v3/set_metadata.pb.h"

#include "common/config/well_known_names.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

#include "extensions/filters/http/well_known_names.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace SetMetadataFilter {

namespace {

void pbStructUpdate(ProtobufWkt::Struct& obj, ProtobufWkt::Struct const& with) {
  auto& obj_fields = *obj.mutable_fields();

  for (auto const& [key, val] : with.fields()) {
    auto& obj_key = obj_fields[key];
    switch (val.kind_case()) {
    // For scalars, the last one wins.
    case ProtobufWkt::Value::kNullValue:
    case ProtobufWkt::Value::kNumberValue:
    case ProtobufWkt::Value::kStringValue:
    case ProtobufWkt::Value::kBoolValue:
      obj_key = val;
      break;
    // If we got a structure, recursively update.
    case ProtobufWkt::Value::kStructValue:
      pbStructUpdate(*obj_key.mutable_struct_value(), val.struct_value());
      break;
    // For lists, append the new values.
    case ProtobufWkt::Value::kListValue: {
      auto& obj_key_vec = *obj_key.mutable_list_value()->mutable_values();
      auto& vals = val.list_value().values();
      obj_key_vec.MergeFrom(vals);
      break;
    }
    case ProtobufWkt::Value::KIND_NOT_SET:
      break;
    }
  }
}

} // namespace

Config::Config(const envoy::extensions::filters::http::set_metadata::v3::Config& proto_config) {
  namespace_ = proto_config.metadata_namespace();
  value_ = proto_config.value();
}

SetMetadataFilter::SetMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

SetMetadataFilter::~SetMetadataFilter() = default;

Http::FilterHeadersStatus SetMetadataFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  const auto metadata_namespace = config_->metadataNamespace();
  auto& metadata = *decoder_callbacks_->streamInfo().dynamicMetadata().mutable_filter_metadata();
  ProtobufWkt::Struct& org_fields = metadata[metadata_namespace];
  ProtobufWkt::Struct const& to_merge = config_->value();

  pbStructUpdate(org_fields, to_merge);

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
