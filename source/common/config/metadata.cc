#include "common/config/metadata.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Config {

const ProtobufWkt::Value& Metadata::metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                  const std::string& filter,
                                                  const std::vector<std::string>& path) {
  const auto filter_it = metadata.filter_metadata().find(filter);
  if (filter_it == metadata.filter_metadata().end()) {
    return ProtobufWkt::Value::default_instance();
  }
  const ProtobufWkt::Struct* data_struct = &(filter_it->second);
  const ProtobufWkt::Value* val = nullptr;
  // go through path to select sub entries
  for (const auto p : path) {
    if (nullptr == data_struct) { // sub entry not found
      return ProtobufWkt::Value::default_instance();
    }
    const auto entry_it = data_struct->fields().find(p);
    if (entry_it == data_struct->fields().end()) {
      return ProtobufWkt::Value::default_instance();
    }
    val = &(entry_it->second);
    if (val->has_struct_value()) {
      data_struct = &(val->struct_value());
    } else {
      data_struct = nullptr;
    }
  }
  if (nullptr == val) {
    return ProtobufWkt::Value::default_instance();
  }
  return *val;
}

const ProtobufWkt::Value& Metadata::metadataValue(const envoy::api::v2::core::Metadata& metadata,
                                                  const std::string& filter,
                                                  const std::string& key) {
  const std::vector<std::string> path{key};
  return metadataValue(metadata, filter, path);
}

ProtobufWkt::Value& Metadata::mutableMetadataValue(envoy::api::v2::core::Metadata& metadata,
                                                   const std::string& filter,
                                                   const std::string& key) {
  return (*(*metadata.mutable_filter_metadata())[filter].mutable_fields())[key];
}

bool Metadata::match(const envoy::api::v2::core::MetadataMatcher& matcher,
                     const envoy::api::v2::core::Metadata& metadata) {
  const std::vector<std::string> path(matcher.path().begin(), matcher.path().end());
  const auto& value = metadataValue(metadata, matcher.filter(), path);
  for (const envoy::api::v2::core::MetadataMatcher::Value& m : matcher.values()) {
    switch (m.match_specifier_case()) {
    case envoy::api::v2::core::MetadataMatcher_Value::kNullMatch:
      if (value.kind_case() == ProtobufWkt::Value::kNullValue && m.null_match()) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kNumberMatch:
      if (value.kind_case() == ProtobufWkt::Value::kNumberValue &&
          m.number_match() == value.number_value()) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kExactMatch:
      if (value.kind_case() == ProtobufWkt::Value::kStringValue &&
          m.exact_match() == value.string_value()) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kPrefixMatch:
      if (value.kind_case() == ProtobufWkt::Value::kStringValue &&
          absl::StartsWith(value.string_value(), m.prefix_match())) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kSuffixMatch:
      if (value.kind_case() == ProtobufWkt::Value::kStringValue &&
          absl::EndsWith(value.string_value(), m.suffix_match())) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kBoolMatch:
      if (value.kind_case() == ProtobufWkt::Value::kBoolValue &&
          m.bool_match() == value.bool_value()) {
        return true;
      }
      break;
    case envoy::api::v2::core::MetadataMatcher_Value::kPresentMatch:
      if (value.kind_case() != ProtobufWkt::Value::KIND_NOT_SET && m.present_match()) {
        return true;
      }
      break;
    default:
      break;
    }
  }
  return false;
}

} // namespace Config
} // namespace Envoy
