#pragma once

#include "envoy/config/bootstrap/v3alpha/bootstrap.pb.h"
#include "envoy/config/cluster/v3alpha/cluster.pb.h"
#include "envoy/config/core/v3alpha/base.pb.h"
#include "envoy/config/core/v3alpha/health_check.pb.h"
#include "envoy/config/endpoint/v3alpha/endpoint_components.pb.h"
#include "envoy/upstream/upstream.h"

#include "common/common/utility.h"
#include "common/json/json_loader.h"
#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "test/test_common/utility.h"

#include "fmt/printf.h"

namespace Envoy {
namespace Upstream {
namespace {

constexpr static const char* kDefaultStaticClusterTmpl = R"EOF(
  {
    "name": "%s",
    "connect_timeout": "0.250s",
    "type": "static",
    "lb_policy": "round_robin",
    "hosts": [
      {
        %s,
      }
    ]
  }
  )EOF";

inline std::string defaultStaticClusterJson(const std::string& name) {
  return fmt::sprintf(kDefaultStaticClusterTmpl, name, R"EOF(
"socket_address": {
  "address": "127.0.0.1",
  "port_value": 11001
})EOF");
}

inline envoy::config::bootstrap::v3alpha::Bootstrap
parseBootstrapFromV2Json(const std::string& json_string) {
  envoy::config::bootstrap::v3alpha::Bootstrap bootstrap;
  TestUtility::loadFromJson(json_string, bootstrap);
  return bootstrap;
}

inline envoy::config::cluster::v3alpha::Cluster
parseClusterFromV2Json(const std::string& json_string) {
  envoy::config::cluster::v3alpha::Cluster cluster;
  TestUtility::loadFromJson(json_string, cluster);
  return cluster;
}

inline envoy::config::cluster::v3alpha::Cluster parseClusterFromV2Yaml(const std::string& yaml) {
  envoy::config::cluster::v3alpha::Cluster cluster;
  TestUtility::loadFromYaml(yaml, cluster);
  return cluster;
}

inline envoy::config::cluster::v3alpha::Cluster defaultStaticCluster(const std::string& name) {
  return parseClusterFromV2Json(defaultStaticClusterJson(name));
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(
      cluster, "", Network::Utility::resolveUrl(url),
      envoy::config::core::v3alpha::Metadata::default_instance(), weight,
      envoy::config::core::v3alpha::Locality(),
      envoy::config::endpoint::v3alpha::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::config::core::v3alpha::UNKNOWN)};
}

inline HostSharedPtr makeTestHost(ClusterInfoConstSharedPtr cluster, const std::string& url,
                                  const envoy::config::core::v3alpha::Metadata& metadata,
                                  uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(
      cluster, "", Network::Utility::resolveUrl(url), metadata, weight,
      envoy::config::core::v3alpha::Locality(),
      envoy::config::endpoint::v3alpha::Endpoint::HealthCheckConfig::default_instance(), 0,
      envoy::config::core::v3alpha::UNKNOWN)};
}

inline HostSharedPtr makeTestHost(
    ClusterInfoConstSharedPtr cluster, const std::string& url,
    const envoy::config::endpoint::v3alpha::Endpoint::HealthCheckConfig& health_check_config,
    uint32_t weight = 1) {
  return HostSharedPtr{new HostImpl(cluster, "", Network::Utility::resolveUrl(url),
                                    envoy::config::core::v3alpha::Metadata::default_instance(),
                                    weight, envoy::config::core::v3alpha::Locality(),
                                    health_check_config, 0, envoy::config::core::v3alpha::UNKNOWN)};
}

inline HostDescriptionConstSharedPtr makeTestHostDescription(ClusterInfoConstSharedPtr cluster,
                                                             const std::string& url) {
  return HostDescriptionConstSharedPtr{new HostDescriptionImpl(
      cluster, "", Network::Utility::resolveUrl(url),
      envoy::config::core::v3alpha::Metadata::default_instance(),
      envoy::config::core::v3alpha::Locality().default_instance(),
      envoy::config::endpoint::v3alpha::Endpoint::HealthCheckConfig::default_instance(), 0)};
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

inline envoy::config::core::v3alpha::HealthCheck
parseHealthCheckFromV2Yaml(const std::string& yaml_string) {
  envoy::config::core::v3alpha::HealthCheck health_check;
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
