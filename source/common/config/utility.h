#pragma once

#include "envoy/api/v2/cds.pb.h"
#include "envoy/api/v2/core/base.pb.h"
#include "envoy/api/v2/eds.pb.h"
#include "envoy/api/v2/lds.pb.h"
#include "envoy/api/v2/route/route.pb.h"
#include "envoy/config/bootstrap/v2/bootstrap.pb.h"
#include "envoy/config/filter/network/http_connection_manager/v2/http_connection_manager.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/json/json_object.h"
#include "envoy/local_info/local_info.h"
#include "envoy/registry/registry.h"
#include "envoy/stats/stats.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/common/assert.h"
#include "common/common/hash.h"
#include "common/common/hex.h"
#include "common/grpc/common.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"
#include "common/singleton/const_singleton.h"

namespace Envoy {
namespace Config {

/**
 * Constant Api Type Values, used by envoy::api::v2::core::ApiConfigSource.
 */
class ApiTypeValues {
public:
  const std::string RestLegacy{"REST_LEGACY"};
  const std::string Rest{"REST"};
  const std::string Grpc{"GRPC"};
};

typedef ConstSingleton<ApiTypeValues> ApiType;

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
    for (const auto& resource : response.resources()) {
      auto* typed_resource = typed_resources.Add();
      resource.UnpackTo(typed_resource);
    }
    return typed_resources;
  }

  /**
   * Legacy APIs uses JSON and do not have an explicit version.
   * @param input the input to hash.
   * @return std::pair<std::string, uint64_t> the string is the hash converted into
   *         a hex string, pre-pended by a user friendly prefix. The uint64_t is the
   *         raw hash.
   */
  static std::pair<std::string, uint64_t> computeHashedVersion(const std::string& input) {
    uint64_t hash = HashUtil::xxHash64(input);
    return std::make_pair("hash_" + Hex::uint64ToHex(hash), hash);
  }

  /**
   * Extract refresh_delay as a std::chrono::milliseconds from
   * envoy::api::v2::core::ApiConfigSource.
   */
  static std::chrono::milliseconds
  apiConfigSourceRefreshDelay(const envoy::api::v2::core::ApiConfigSource& api_config_source);

  /**
   * Populate an envoy::api::v2::core::ApiConfigSource.
   * @param cluster supplies the cluster name for the ApiConfigSource.
   * @param refresh_delay_ms supplies the refresh delay for the ApiConfigSource in ms.
   * @param api_type supplies the type of subscription to use for the ApiConfigSource.
   * @param api_config_source a reference to the envoy::api::v2::core::ApiConfigSource object to
   * populate.
   */
  static void translateApiConfigSource(const std::string& cluster, uint32_t refresh_delay_ms,
                                       const std::string& api_type,
                                       envoy::api::v2::core::ApiConfigSource& api_config_source);

  /**
   * Check cluster info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   */
  static void checkCluster(const std::string& error_prefix, const std::string& cluster_name,
                           Upstream::ClusterManager& cm);

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
   * Check the existence of a path for a filesystem subscription. Throws on error.
   * @param path the path to validate.
   */
  static void checkFilesystemSubscriptionBackingPath(const std::string& path);

  /**
   * Check the validity of a cluster backing an api config source. Throws on error.
   * @param clusters the clusters currently loaded in the cluster manager.
   * @param api_config_source the config source to validate.
   */
  static void checkApiConfigSourceSubscriptionBackingCluster(
      const Upstream::ClusterManager::ClusterInfoMap& clusters,
      const envoy::api::v2::core::ApiConfigSource& api_config_source);

  /**
   * Convert a v1 SDS JSON config to v2 EDS envoy::api::v2::core::ConfigSource.
   * @param json_config source v1 SDS JSON config.
   * @param eds_config destination v2 EDS envoy::api::v2::core::ConfigSource.
   */
  static void translateEdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::core::ConfigSource& eds_config);

  /**
   * Convert a v1 CDS JSON config to v2 CDS envoy::api::v2::core::ConfigSource.
   * @param json_config source v1 CDS JSON config.
   * @param cds_config destination v2 CDS envoy::api::v2::core::ConfigSource.
   */
  static void translateCdsConfig(const Json::Object& json_config,
                                 envoy::api::v2::core::ConfigSource& cds_config);

  /**
   * Convert a v1 RDS JSON config to v2 RDS
   * envoy::config::filter::network::http_connection_manager::v2::Rds.
   * @param json_rds source v1 RDS JSON config.
   * @param rds destination v2 RDS envoy::config::filter::network::http_connection_manager::v2::Rds.
   */
  static void
  translateRdsConfig(const Json::Object& json_rds,
                     envoy::config::filter::network::http_connection_manager::v2::Rds& rds);

  /**
   * Convert a v1 LDS JSON config to v2 LDS envoy::api::v2::core::ConfigSource.
   * @param json_lds source v1 LDS JSON config.
   * @param lds_config destination v2 LDS envoy::api::v2::core::ConfigSource.
   */
  static void translateLdsConfig(const Json::Object& json_lds,
                                 envoy::api::v2::core::ConfigSource& lds_config);

  /**
   * Generate a SubscriptionStats object from stats scope.
   * @param scope for stats.
   * @return SubscriptionStats for scope.
   */
  static SubscriptionStats generateStats(Stats::Scope& scope) {
    return {ALL_SUBSCRIPTION_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope))};
  }

  /**
   * Get a Factory from the registry with a particular name (and templated type) with error checking
   * to ensure the name and factory are valid.
   * @param name string identifier for the particular implementation. Note: this is a proto string
   * because it is assumed that this value will be pulled directly from the configuration proto.
   */
  template <class Factory> static Factory& getAndCheckFactory(const ProtobufTypes::String& name) {
    if (name.empty()) {
      throw EnvoyException("Provided name for static registration lookup was empty.");
    }

    Factory* factory = Registry::FactoryRegistry<Factory>::getFactory(name);

    if (factory == nullptr) {
      throw EnvoyException(
          fmt::format("Didn't find a registered implementation for name: '{}'", name));
    }

    return *factory;
  }

  /**
   * Translate a nested config into a proto message provided by the implementation factory.
   * @param enclosing_message proto that contains a field 'config'. Note: the enclosing proto is
   * provided because for statically registered implementations, a custom config is generally
   * optional, which means the conversion must be done conditionally.
   * @param factory implementation factory with the method 'createEmptyConfigProto' to produce a
   * proto to be filled with the translated configuration.
   */
  template <class ProtoMessage, class Factory>
  static ProtobufTypes::MessagePtr translateToFactoryConfig(const ProtoMessage& enclosing_message,
                                                            Factory& factory) {
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

    if (config == nullptr) {
      throw EnvoyException(fmt::format(
          "{} factory returned nullptr instead of empty config message.", factory.name()));
    }

    if (enclosing_message.has_config()) {
      MessageUtil::jsonConvert(enclosing_message.config(), *config);
    }

    return config;
  }

  /**
   * Obtain the "name" of a v2 API resource in a google.protobuf.Any, e.g. the route config name for
   * a Routeconfiguration, based on the underlying resource type.
   * TODO(htuch): This is kind of a hack. If we had a better support for resource names as first
   * class in the API, this would not be necessary.
   * @param resource google.protobuf.Any v2 API resource.
   * @return std::string resource name.
   */
  static std::string resourceName(const ProtobufWkt::Any& resource);

  /**
   * Create TagProducer instance. Check all tag names for conflicts to avoid
   * unexpected tag name overwriting.
   * @param bootstrap bootstrap proto.
   * @throws EnvoyException when the conflict of tag names is found.
   */
  static Stats::TagProducerPtr
  createTagProducer(const envoy::config::bootstrap::v2::Bootstrap& bootstrap);

  /**
   * Check user supplied name in RDS/CDS/LDS for sanity.
   * It should be within the configured length limit. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param name supplies the name to check for length limits.
   */
  static void checkObjNameLength(const std::string& error_prefix, const std::string& name);

  /**
   * Obtain gRPC async client factory from a envoy::api::v2::core::ApiConfigSource.
   * @param async_client_manager gRPC async client manager.
   * @param api_config_source envoy::api::v2::core::ApiConfigSource. Must have config type GRPC.
   * @return Grpc::AsyncClientFactoryPtr gRPC async client factory.
   */
  static Grpc::AsyncClientFactoryPtr
  factoryForApiConfigSource(Grpc::AsyncClientManager& async_client_manager,
                            const envoy::api::v2::core::ApiConfigSource& api_config_source,
                            Stats::Scope& scope);
};

} // namespace Config
} // namespace Envoy
