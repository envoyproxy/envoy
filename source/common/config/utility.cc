#include "common/config/utility.h"

#include "common/common/assert.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"

#include "fmt/format.h"

namespace Envoy {
namespace Config {

void Utility::translateApiConfigSource(const std::string& cluster, uint32_t refresh_delay_ms,
                                       const std::string& api_type,
                                       envoy::api::v2::ApiConfigSource& api_config_source) {
  // TODO(junr03): document the option to chose an api type once we have created
  // stronger constraints around v2.
  if (api_type == ApiType::get().RestLegacy) {
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::REST_LEGACY);
  } else if (api_type == ApiType::get().Rest) {
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::REST);
  } else {
    ASSERT(api_type == ApiType::get().Grpc);
    api_config_source.set_api_type(envoy::api::v2::ApiConfigSource::GRPC);
  }
  api_config_source.add_cluster_name(cluster);
  api_config_source.mutable_refresh_delay()->CopyFrom(
      Protobuf::util::TimeUtil::MillisecondsToDuration(refresh_delay_ms));
}

void Utility::checkCluster(const std::string& error_prefix, const std::string& cluster_name,
                           Upstream::ClusterManager& cm) {
  Upstream::ThreadLocalCluster* cluster = cm.get(cluster_name);
  if (cluster == nullptr) {
    throw EnvoyException(fmt::format("{}: unknown cluster '{}'", error_prefix, cluster_name));
  }

  if (cluster->info()->addedViaApi()) {
    throw EnvoyException(fmt::format("{}: invalid cluster '{}': currently only "
                                     "static (non-CDS) clusters are supported",
                                     error_prefix, cluster_name));
  }
}

void Utility::checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info) {
  checkCluster(error_prefix, cluster_name, cm);
  checkLocalInfo(error_prefix, local_info);
}

void Utility::checkLocalInfo(const std::string& error_prefix,
                             const LocalInfo::LocalInfo& local_info) {
  if (local_info.clusterName().empty() || local_info.nodeName().empty()) {
    throw EnvoyException(
        fmt::format("{}: setting --service-cluster and --service-node is required", error_prefix));
  }
}

std::chrono::milliseconds
Utility::apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source) {
  return std::chrono::milliseconds(
      Protobuf::util::TimeUtil::DurationToMilliseconds(api_config_source.refresh_delay()));
}

void Utility::translateEdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::ConfigSource& eds_config) {
  translateApiConfigSource(json_config.getObject("cluster")->getString("name"),
                           json_config.getInteger("refresh_delay_ms", 30000),
                           json_config.getString("api_type", ApiType::get().RestLegacy),
                           *eds_config.mutable_api_config_source());
}

void Utility::translateCdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::ConfigSource& cds_config) {
  translateApiConfigSource(json_config.getObject("cluster")->getString("name"),
                           json_config.getInteger("refresh_delay_ms", 30000),
                           json_config.getString("api_type", ApiType::get().RestLegacy),
                           *cds_config.mutable_api_config_source());
}

void Utility::translateRdsConfig(const Json::Object& json_rds, envoy::api::v2::filter::Rds& rds) {
  json_rds.validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);
  translateApiConfigSource(json_rds.getString("cluster"),
                           json_rds.getInteger("refresh_delay_ms", 30000),
                           json_rds.getString("api_type", ApiType::get().RestLegacy),
                           *rds.mutable_config_source()->mutable_api_config_source());
  JSON_UTIL_SET_STRING(json_rds, rds, route_config_name);
}

void Utility::translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::ConfigSource& lds_config) {
  json_lds.validateSchema(Json::Schema::LDS_CONFIG_SCHEMA);
  translateApiConfigSource(json_lds.getString("cluster"),
                           json_lds.getInteger("refresh_delay_ms", 30000),
                           json_lds.getString("api_type", ApiType::get().RestLegacy),
                           *lds_config.mutable_api_config_source());
}

} // namespace Config
} // namespace Envoy
