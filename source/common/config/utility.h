#pragma once

#include "envoy/local_info/local_info.h"
#include "envoy/upstream/cluster_manager.h"

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
   * @return google::protobuf::RepatedPtrField<ResourceType> vector of typed resources in response.
   */
  template <class ResourceType>
  static google::protobuf::RepeatedPtrField<ResourceType>
  getTypedResources(const envoy::api::v2::DiscoveryResponse& response) {
    google::protobuf::RepeatedPtrField<ResourceType> typed_resources;
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
   * Lookup value of a key for a given filter in Metadata.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return const google::protobuf::Value& value if found, empty if not found.
   */
  static const google::protobuf::Value& metadataValue(const envoy::api::v2::Metadata& metadata,
                                                      const std::string& filter,
                                                      const std::string& key);

  /**
   * Obtain mutable reference to metadata value for a given filter and key.
   * @param metadata reference.
   * @param filter name.
   * @param key for filter metadata.
   * @return google::protobuf::Value&. A Value message is created if not found.
   */
  static google::protobuf::Value& mutableMetadataValue(envoy::api::v2::Metadata& metadata,
                                                       const std::string& filter,
                                                       const std::string& key);
};

} // Config
} // Envoy
