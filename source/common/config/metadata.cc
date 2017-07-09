#include "common/config/metadata.h"

namespace Envoy {
namespace Config {

const google::protobuf::Value& Metadata::metadataValue(const envoy::api::v2::Metadata& metadata,
                                                       const std::string& filter,
                                                       const std::string& key) {
  const auto filter_it = metadata.filter_metadata().find(filter);
  if (filter_it == metadata.filter_metadata().end()) {
    return google::protobuf::Value::default_instance();
  }
  const auto fields_it = filter_it->second.fields().find(key);
  if (fields_it == filter_it->second.fields().end()) {
    return google::protobuf::Value::default_instance();
  }
  return fields_it->second;
}

google::protobuf::Value& Metadata::mutableMetadataValue(envoy::api::v2::Metadata& metadata,
                                                        const std::string& filter,
                                                        const std::string& key) {
  return (*(*metadata.mutable_filter_metadata())[filter].mutable_fields())[key];
}

} // namespace Config
} // namespace Envoy
