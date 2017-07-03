#include "common/config/utility.h"

#include "google/protobuf/util/time_util.h"

namespace Envoy {
namespace Config {

void Utility::localInfoToNode(const LocalInfo::LocalInfo& local_info, envoy::api::v2::Node& node) {
  node.set_id(local_info.nodeName());
  node.mutable_locality()->set_zone(local_info.zoneName());
  // TODO(htuch): Fill in more fields, e.g. cluster name in metadata, etc.
}

std::chrono::milliseconds
Utility::apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      google::protobuf::util::TimeUtil::DurationToMilliseconds(api_config_source.refresh_delay()));
}

void Utility::sdsConfigToEdsConfig(const Upstream::SdsConfig& sds_config,
                                   envoy::api::v2::ConfigSource& eds_config) {
  auto* api_config_source = eds_config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::ApiConfigSource::REST_LEGACY);
  api_config_source->add_cluster_name(sds_config.sds_cluster_name_);
  api_config_source->mutable_refresh_delay()->CopyFrom(
      google::protobuf::util::TimeUtil::MillisecondsToDuration(sds_config.refresh_delay_.count()));
}

const google::protobuf::Value& Utility::metadataValue(const envoy::api::v2::Metadata& metadata,
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

google::protobuf::Value& Utility::mutableMetadataValue(envoy::api::v2::Metadata& metadata,
                                                       const std::string& filter,
                                                       const std::string& key) {
  return (*(*metadata.mutable_filter_metadata())[filter].mutable_fields())[key];
}

} // namespace Config
} // namespace Envoy
