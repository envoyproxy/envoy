#include "common/config/utility.h"

#include "common/protobuf/protobuf.h"

#include "spdlog/spdlog.h"

namespace Envoy {
namespace Config {

void Utility::checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info) {
  if (!cm.get(cluster_name)) {
    throw EnvoyException(fmt::format("{}: unknown cluster: {}", error_prefix, cluster_name));
  }

  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("{}: setting --service-cluster and --service-node is required", error_prefix));
  }
}

void Utility::localInfoToNode(const LocalInfo::LocalInfo& local_info, envoy::api::v2::Node& node) {
  node.set_id(local_info.nodeName());
  node.mutable_locality()->set_zone(local_info.zoneName());
  // TODO(htuch): Fill in more fields, e.g. cluster name in metadata, etc.
}

std::chrono::milliseconds
Utility::apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      Protobuf::util::TimeUtil::DurationToMilliseconds(api_config_source.refresh_delay()));
}

void Utility::sdsConfigToEdsConfig(const Upstream::SdsConfig& sds_config,
                                   envoy::api::v2::ConfigSource& eds_config) {
  auto* api_config_source = eds_config.mutable_api_config_source();
  api_config_source->set_api_type(envoy::api::v2::ApiConfigSource::REST_LEGACY);
  api_config_source->add_cluster_name(sds_config.sds_cluster_name_);
  api_config_source->mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(sds_config.refresh_delay_.count()));
}

} // namespace Config
} // namespace Envoy
