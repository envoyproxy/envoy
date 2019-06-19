#pragma once

#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/config/cds_json.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/test_common/utility.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Upstream {
namespace {

inline std::string defaultStaticClusterJson(const std::string& name) {
  return fmt::sprintf(R"EOF(
  {
    "name": "%s",
    "connect_timeout": "0.250s",
    "type": "static",
    "lb_policy": "round_robin",
    "hosts": [
      {
        "socket_address": {
          "address": "127.0.0.1",
          "port_value": 11001
        }
      }
    ]
  }
  )EOF",
                      name);
}

inline envoy::config::bootstrap::v2::Bootstrap
parseBootstrapFromV2Json(const std::string& json_string) {
  envoy::config::bootstrap::v2::Bootstrap bootstrap;
  TestUtility::loadFromJson(json_string, bootstrap);
  return bootstrap;
}

inline envoy::api::v2::Cluster parseClusterFromJson(const std::string& json_string) {
  envoy::api::v2::Cluster cluster;
  auto json_object_ptr = Json::Factory::loadFromString(json_string);
  Config::CdsJson::translateCluster(*json_object_ptr,
                                    absl::optional<envoy::api::v2::core::ConfigSource>(), cluster);
  return cluster;
}

inline envoy::api::v2::Cluster parseClusterFromV2Json(const std::string& json_string) {
  envoy::api::v2::Cluster cluster;
  TestUtility::loadFromJson(json_string, cluster);
  return cluster;
}

inline envoy::api::v2::Cluster parseClusterFromV2Yaml(const std::string& yaml) {
  envoy::api::v2::Cluster cluster;
  TestUtility::loadFromYaml(yaml, cluster);
  return cluster;
}

inline envoy::api::v2::Cluster defaultStaticCluster(const std::string& name) {
  return parseClusterFromV2Json(defaultStaticClusterJson(name));
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(
      cluster, "", Network::Utility::resolveUrl(url),
      envoy::api::v2::core::Metadata::default_instance(), weight, envoy::api::v2::core::Locality(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::api::v2::core::HealthStatus::UNKNOWN)};
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  const envoy::api::v2::core::Metadata& metadata,
                                  uint32_t weight = 1) {
  return HostSharedPtr{
      new HostImpl(cluster, "", Network::Utility::resolveUrl(url), metadata, weight,
                   envoy::api::v2::core::Locality(),
                   envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0,
                   envoy::api::v2::core::HealthStatus::UNKNOWN)};
}

inline HostSharedPtr
makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
             const envoy::api::v2::endpoint::Endpoint::HealthCheckConfig& health_check_config,
             uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(cluster, "", Network::Utility::resolveUrl(url),
                                    envoy::api::v2::core::Metadata::default_instance(), weight,
                                    envoy::api::v2::core::Locality(), health_check_config, 0,
                                    envoy::api::v2::core::HealthStatus::UNKNOWN)};
}

inline HostDescriptionConstSharedPtr makeTestHostDescription(ClusterInfoConstSharedPtr cluster,
                                                             const std::string& url) {
  return HostDescriptionConstSharedPtr{new HostDescriptionImpl(
      cluster, "", Network::Utility::resolveUrl(url),
      envoy::api::v2::core::Metadata::default_instance(),
      envoy::api::v2::core::Locality().default_instance(),
      envoy::api::v2::endpoint::Endpoint::HealthCheckConfig::default_instance(), 0)};
}

inline HostsPerLocalitySharedPtr makeHostsPerLocality(std::vector<HostVector>&& locality_hosts,
                                                      bool force_no_local_locality = false) {
  return std::make_shared<HostsPerLocalityImpl>(
      std::move(locality_hosts), !force_no_local_locality && !locality_hosts.empty());
}

inline LocalityWeightsSharedPtr
makeLocalityWeights(std::initializer_list<uint32_t> locality_weights) {
  return std::make_shared<LocalityWeights>(locality_weights);
}

inline envoy::api::v2::core::HealthCheck
parseHealthCheckFromV2Yaml(const std::string& yaml_string) {
  envoy::api::v2::core::HealthCheck health_check;
  TestUtility::loadFromYaml(yaml_string, health_check);
  return health_check;
}

inline PrioritySet::UpdateHostsParams
updateHostsParams(HostVectorConstSharedPtr hosts, HostsPerLocalityConstSharedPtr hosts_per_locality,
                  HealthyHostVectorConstSharedPtr healthy_hosts,
                  HostsPerLocalityConstSharedPtr healthy_hosts_per_locality) {
  return HostSetImpl::updateHostsParams(
      hosts, hosts_per_locality, std::move(healthy_hosts), std::move(healthy_hosts_per_locality),
      std::make_shared<const DegradedHostVector>(), HostsPerLocalityImpl::empty(),
      std::make_shared<const ExcludedHostVector>(), HostsPerLocalityImpl::empty());
}

inline PrioritySet::UpdateHostsParams
updateHostsParams(HostVectorConstSharedPtr hosts,
                  HostsPerLocalityConstSharedPtr hosts_per_locality) {
  return updateHostsParams(std::move(hosts), std::move(hosts_per_locality),
                           std::make_shared<const HealthyHostVector>(),
                           HostsPerLocalityImpl::empty());
}

} // namespace
} // namespace Upstream
} // namespace Envoy
