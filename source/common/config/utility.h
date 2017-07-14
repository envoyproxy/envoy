#pragma once

#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/protobuf/protobuf.h"

#include "api/base.pb.h"

namespace Envoy {
namespace Config {

/**
 * General config API utilities.
 */
class Utility {
public:
  /**
   * Extract typed resources from a DiscoveryResponse.
   * @param response reference to DiscoveryResponse.
   * @return Protobuf::RepatedPtrField<ResourceType> vector of typed resources in response.
   */
  template <class ResourceType>
  static Protobuf::RepeatedPtrField<ResourceType>
  getTypedResources(const envoy::api::v2::DiscoveryResponse& response) {
    Protobuf::RepeatedPtrField<ResourceType> typed_resources;
    for (auto& resource : response.resources()) {
      auto* typed_resource = typed_resources.Add();
      resource.UnpackTo(typed_resource);
    }
    return typed_resources;
  }

  /**
   * Extract refresh_delay as a std::chrono::milliseconds from envoy::api::v2::ApiConfigSource.
   */
  static std::chrono::milliseconds
  apiConfigSourceRefreshDelay(const envoy::api::v2::ApiConfigSource& api_config_source);

  /**
   * Check cluster/local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   * @param local_info supplies the local info.
   */
  static void checkClusterAndLocalInfo(const std::string& error_prefix,
                                       const std::string& cluster_name,
                                       Upstream::ClusterManager& cm,
                                       const LocalInfo::LocalInfo& local_info);

  /**
   * Check local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param local_info supplies the local info.
   */
  static void checkLocalInfo(const std::string& error_prefix,
                             const LocalInfo::LocalInfo& local_info);

  /**
   * Convert LocalInfo::LocalInfo to v2 envoy::api::v2::Node identifier.
   * @param local_info source LocalInfo::LocalInfo.
   * @param node destination envoy::api::Node.
   */
  static void localInfoToNode(const LocalInfo::LocalInfo& local_info, envoy::api::v2::Node& node);

  /**
   * Convert a v1 SdsConfig to v2 EDS envoy::api::v2::ConfigSource.
   * @param sds_config source v1 SdsConfig.
   * @param eds_config destination v2 EDS envoy::api::v2::ConfigSource.
   */
  static void sdsConfigToEdsConfig(const Upstream::SdsConfig& sds_config,
                                   envoy::api::v2::ConfigSource& eds_config);

  /**
   * Generate a SubscriptionStats object from stats scope.
   * @param scope for stats.
   * @return SubscriptionStats for scope.
   */
  static SubscriptionStats generateStats(Stats::Scope& scope) {
    return {ALL_SUBSCRIPTION_STATS(POOL_COUNTER(scope))};
  }
};

} // namespace Config
} // namespace Envoy
