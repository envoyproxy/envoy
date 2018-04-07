#include "common/config/metadata.h"

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

} // namespace Config
} // namespace Envoy
