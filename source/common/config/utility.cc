#include "common/config/utility.h"

#include <unordered_set>

#include "envoy/config/metrics/v2/stats.pb.h"

#include "common/common/assert.h"
#include "common/common/fmt.h"
#include "common/common/hex.h"
#include "common/common/utility.h"
#include "common/config/json_utility.h"
#include "common/config/resources.h"
#include "common/config/well_known_names.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/json/config_schemas.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/stats/stats_impl.h"

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
  api_config_source.add_cluster_names(cluster);
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

void Utility::checkFilesystemSubscriptionBackingPath(const std::string& path) {
  // TODO(junr03): the file might be deleted between this check and the
  // watch addition.
  if (!Filesystem::fileExists(path)) {
    throw EnvoyException(fmt::format(
        "envoy::api::v2::Path must refer to an existing path in the system: '{}' does not exist",
        path));
  }
}

void Utility::checkApiConfigSourceSubscriptionBackingCluster(
    const Upstream::ClusterManager::ClusterInfoMap& clusters,
    const envoy::api::v2::ApiConfigSource& api_config_source) {
  if (api_config_source.cluster_names().size() != 1) {
    // TODO(htuch): Add support for multiple clusters, #1170.
    throw EnvoyException(
        "envoy::api::v2::ConfigSource must have a singleton cluster name specified");
  }

  const auto& cluster_name = api_config_source.cluster_names()[0];
  const auto& it = clusters.find(cluster_name);
  if (it == clusters.end() || it->second.get().info()->addedViaApi() ||
      it->second.get().info()->type() == envoy::api::v2::Cluster::EDS) {
    throw EnvoyException(fmt::format(
        "envoy::api::v2::ConfigSource must have a statically "
        "defined non-EDS cluster: '{}' does not exist, was added via api, or is an EDS cluster",
        cluster_name));
  }
}

std::chrono::milliseconds
Utility::apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source) {
  if (!api_config_source.has_refresh_delay()) {
    throw EnvoyException("refresh_delay is required for REST API configuration sources");
  }

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

void Utility::translateRdsConfig(const Json::Object& json_rds,
                                 envoy::api::v2::filter::network::Rds& rds) {
  json_rds.validateSchema(Json::Schema::RDS_CONFIGURATION_SCHEMA);

  const std::string name = json_rds.getString("route_config_name", "");
  checkObjNameLength("Invalid route_config name", name);
  rds.set_route_config_name(name);

  translateApiConfigSource(json_rds.getString("cluster"),
                           json_rds.getInteger("refresh_delay_ms", 30000),
                           json_rds.getString("api_type", ApiType::get().RestLegacy),
                           *rds.mutable_config_source()->mutable_api_config_source());
}

void Utility::translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::ConfigSource& lds_config) {
  json_lds.validateSchema(Json::Schema::LDS_CONFIG_SCHEMA);
  translateApiConfigSource(json_lds.getString("cluster"),
                           json_lds.getInteger("refresh_delay_ms", 30000),
                           json_lds.getString("api_type", ApiType::get().RestLegacy),
                           *lds_config.mutable_api_config_source());
}

std::string Utility::resourceName(const ProtobufWkt::Any& resource) {
  if (resource.type_url() == Config::TypeUrl::get().Listener) {
    return MessageUtil::anyConvert<envoy::api::v2::Listener>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().RouteConfiguration) {
    return MessageUtil::anyConvert<envoy::api::v2::RouteConfiguration>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().Cluster) {
    return MessageUtil::anyConvert<envoy::api::v2::Cluster>(resource).name();
  }
  if (resource.type_url() == Config::TypeUrl::get().ClusterLoadAssignment) {
    return MessageUtil::anyConvert<envoy::api::v2::ClusterLoadAssignment>(resource).cluster_name();
  }
  throw EnvoyException(
      fmt::format("Unknown type URL {} in DiscoveryResponse", resource.type_url()));
}

Stats::TagProducerPtr
Utility::createTagProducer(const envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
  return std::make_unique<Stats::TagProducerImpl>(bootstrap.stats_config());
}

void Utility::checkObjNameLength(const std::string& error_prefix, const std::string& name) {
  if (name.length() > Stats::RawStatData::maxObjNameLength()) {
    throw EnvoyException(fmt::format("{}: Length of {} ({}) exceeds allowed maximum length ({})",
                                     error_prefix, name, name.length(),
                                     Stats::RawStatData::maxObjNameLength()));
  }
}

Grpc::AsyncClientFactoryPtr
Utility::factoryForApiConfigSource(Grpc::AsyncClientManager& async_client_manager,
                                   const envoy::api::v2::ApiConfigSource& api_config_source,
                                   Stats::Scope& scope) {
  ASSERT(api_config_source.api_type() == envoy::api::v2::ApiConfigSource::GRPC);
  envoy::api::v2::GrpcService grpc_service;
  if (api_config_source.cluster_names().empty()) {
    if (api_config_source.grpc_services().empty()) {
      throw EnvoyException(
          fmt::format("Missing gRPC services in envoy::api::v2::ApiConfigSource: {}",
                      api_config_source.DebugString()));
    }
    // TODO(htuch): Implement multiple gRPC services.
    if (api_config_source.grpc_services().size() != 1) {
      throw EnvoyException(fmt::format(
          "Only singleton gRPC service lists supported in envoy::api::v2::ApiConfigSource: {}",
          api_config_source.DebugString()));
    }
    grpc_service.MergeFrom(api_config_source.grpc_services(0));
  } else {
    // TODO(htuch): cluster_names is deprecated, remove after 1.6.0.
    if (api_config_source.cluster_names().size() != 1) {
      throw EnvoyException(fmt::format(
          "Only singleton cluster name lists supported in envoy::api::v2::ApiConfigSource: {}",
          api_config_source.DebugString()));
    }
    grpc_service.mutable_envoy_grpc()->set_cluster_name(api_config_source.cluster_names(0));
  }

  return async_client_manager.factoryForGrpcService(grpc_service, scope);
}

} // namespace Config
} // namespace Envoy
