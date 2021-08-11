#pragma once

#include "envoy/api/api.h"
#include "envoy/common/random_generator.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/address.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/endpoint/v3/endpoint.pb.h"
#include "envoy/config/grpc_mux.h"
#include "envoy/config/subscription.h"
#include "envoy/local_info/local_info.h"
#include "envoy/registry/registry.h"
#include "envoy/server/filter_config.h"
#include "envoy/stats/histogram.h"
#include "envoy/stats/scope.h"
#include "envoy/stats/stats_macros.h"
#include "envoy/stats/stats_matcher.h"
#include "envoy/stats/tag_producer.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/common/backoff_strategy.h"
#include "source/common/common/hash.h"
#include "source/common/common/hex.h"
#include "source/common/common/utility.h"
#include "source/common/grpc/common.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/common/runtime/runtime_features.h"
#include "source/common/singleton/const_singleton.h"

#include "udpa/type/v1/typed_struct.pb.h"

namespace Envoy {
namespace Config {

/**
 * Constant Api Type Values, used by envoy::config::core::v3::ApiConfigSource.
 */
class ApiTypeValues {
public:
  const std::string UnsupportedRestLegacy{"REST_LEGACY"};
  const std::string Rest{"REST"};
  const std::string Grpc{"GRPC"};
};

/**
 * RateLimitSettings for discovery requests.
 */
struct RateLimitSettings {
  // Default Max Tokens.
  static const uint32_t DefaultMaxTokens = 100;
  // Default Fill Rate.
  static constexpr double DefaultFillRate = 10;

  uint32_t max_tokens_{DefaultMaxTokens};
  double fill_rate_{DefaultFillRate};
  bool enabled_{false};
};

using ApiType = ConstSingleton<ApiTypeValues>;

/**
 * General config API utilities.
 */
class Utility {
public:
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
   * envoy::config::core::v3::ApiConfigSource.
   */
  static std::chrono::milliseconds
  apiConfigSourceRefreshDelay(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Extract request_timeout as a std::chrono::milliseconds from
   * envoy::config::core::v3::ApiConfigSource. If request_timeout isn't set in the config source, a
   * default value of 1s will be returned.
   */
  static std::chrono::milliseconds
  apiConfigSourceRequestTimeout(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Extract initial_fetch_timeout as a std::chrono::milliseconds from
   * envoy::config::core::v3::ApiConfigSource. If request_timeout isn't set in the config source, a
   * default value of 0s will be returned.
   */
  static std::chrono::milliseconds
  configSourceInitialFetchTimeout(const envoy::config::core::v3::ConfigSource& config_source);

  /**
   * Populate an envoy::config::core::v3::ApiConfigSource.
   * @param cluster supplies the cluster name for the ApiConfigSource.
   * @param refresh_delay_ms supplies the refresh delay for the ApiConfigSource in ms.
   * @param api_type supplies the type of subscription to use for the ApiConfigSource.
   * @param api_config_source a reference to the envoy::config::core::v3::ApiConfigSource object to
   * populate.
   */
  static void translateApiConfigSource(const std::string& cluster, uint32_t refresh_delay_ms,
                                       const std::string& api_type,
                                       envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Check cluster info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   * @param allow_added_via_api indicates whether a cluster is allowed to be added via api
   *                            rather than be a static resource from the bootstrap config.
   * @return the main thread cluster if it exists.
   */
  static Upstream::ClusterConstOptRef checkCluster(absl::string_view error_prefix,
                                                   absl::string_view cluster_name,
                                                   Upstream::ClusterManager& cm,
                                                   bool allow_added_via_api = false);

  /**
   * Check cluster/local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param cluster_name supplies the cluster name to check.
   * @param cm supplies the cluster manager.
   * @param local_info supplies the local info.
   * @return the main thread cluster if it exists.
   */
  static Upstream::ClusterConstOptRef
  checkClusterAndLocalInfo(absl::string_view error_prefix, absl::string_view cluster_name,
                           Upstream::ClusterManager& cm, const LocalInfo::LocalInfo& local_info);

  /**
   * Check local info for API config sanity. Throws on error.
   * @param error_prefix supplies the prefix to use in error messages.
   * @param local_info supplies the local info.
   */
  static void checkLocalInfo(absl::string_view error_prefix,
                             const LocalInfo::LocalInfo& local_info);

  /**
   * Check the existence of a path for a filesystem subscription. Throws on error.
   * @param path the path to validate.
   * @param api reference to the Api object
   */
  static void checkFilesystemSubscriptionBackingPath(const std::string& path, Api::Api& api);

  /**
   * Check the grpc_services and cluster_names for API config sanity. Throws on error.
   * @param api_config_source the config source to validate.
   * @throws EnvoyException when an API config has the wrong number of gRPC
   * services or cluster names, depending on expectations set by its API type.
   */
  static void
  checkApiConfigSourceNames(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Check the validity of a cluster backing an api config source. Throws on error.
   * @param primary_clusters the API config source eligible clusters.
   * @param cluster_name the cluster name to validate.
   * @param config_source the config source typed name.
   * @throws EnvoyException when an API config doesn't have a statically defined non-EDS cluster.
   */
  static void validateClusterName(const Upstream::ClusterManager::ClusterSet& primary_clusters,
                                  const std::string& cluster_name,
                                  const std::string& config_source);

  /**
   * Potentially calls Utility::validateClusterName, if a cluster name can be found.
   * @param primary_clusters the API config source eligible clusters.
   * @param api_config_source the config source to validate.
   * @throws EnvoyException when an API config doesn't have a statically defined non-EDS cluster.
   */
  static void checkApiConfigSourceSubscriptionBackingCluster(
      const Upstream::ClusterManager::ClusterSet& primary_clusters,
      const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Access transport_api_version field in ApiConfigSource, while validating version
   * compatibility.
   * @param api_config_source the config source to extract transport API version from.
   * @return envoy::config::core::v3::ApiVersion transport API version
   * @throws DeprecatedMajorVersionException when the transport version is disabled.
   */
  template <class Proto>
  static envoy::config::core::v3::ApiVersion
  getAndCheckTransportVersion(const Proto& api_config_source) {
    const auto transport_api_version = api_config_source.transport_api_version();
    ASSERT(Thread::MainThread::isMainThread());
    if (transport_api_version == envoy::config::core::v3::ApiVersion::AUTO ||
        transport_api_version == envoy::config::core::v3::ApiVersion::V2) {
      Runtime::LoaderSingleton::getExisting()->countDeprecatedFeatureUse();
      const std::string& warning = fmt::format(
          "V2 (and AUTO) xDS transport protocol versions are deprecated in {}. "
          "The v2 xDS major version is deprecated and disabled by default. Support for v2 will be "
          "removed from Envoy at the start of Q1 2021. You may make use of v2 in Q4 2020 by "
          "following the advice in https://www.envoyproxy.io/docs/envoy/latest/faq/api/transition.",
          api_config_source.DebugString());
      ENVOY_LOG_MISC(warn, warning);
      if (!Runtime::runtimeFeatureEnabled(
              "envoy.test_only.broken_in_production.enable_deprecated_v2_api")) {
        throw DeprecatedMajorVersionException(warning);
      }
    }
    return transport_api_version;
  }

  /**
   * Parses RateLimit configuration from envoy::config::core::v3::ApiConfigSource to
   * RateLimitSettings.
   * @param api_config_source ApiConfigSource.
   * @return RateLimitSettings.
   */
  static RateLimitSettings
  parseRateLimitSettings(const envoy::config::core::v3::ApiConfigSource& api_config_source);

  /**
   * Generate a ControlPlaneStats object from stats scope.
   * @param scope for stats.
   * @return ControlPlaneStats for scope.
   */
  static ControlPlaneStats generateControlPlaneStats(Stats::Scope& scope) {
    const std::string control_plane_prefix = "control_plane.";
    return {ALL_CONTROL_PLANE_STATS(POOL_COUNTER_PREFIX(scope, control_plane_prefix),
                                    POOL_GAUGE_PREFIX(scope, control_plane_prefix),
                                    POOL_TEXT_READOUT_PREFIX(scope, control_plane_prefix))};
  }

  /**
   * Generate a SubscriptionStats object from stats scope.
   * @param scope for stats.
   * @return SubscriptionStats for scope.
   */
  static SubscriptionStats generateStats(Stats::Scope& scope) {
    return {ALL_SUBSCRIPTION_STATS(POOL_COUNTER(scope), POOL_GAUGE(scope), POOL_TEXT_READOUT(scope),
                                   POOL_HISTOGRAM(scope))};
  }

  /**
   * Get a Factory from the registry with a particular name (and templated type) with error checking
   * to ensure the name and factory are valid.
   * @param name string identifier for the particular implementation.
   * @param is_optional exception will be throw when the value is false and no factory found.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory>
  static Factory* getAndCheckFactoryByName(const std::string& name, bool is_optional) {
    if (name.empty()) {
      ExceptionUtil::throwEnvoyException("Provided name for static registration lookup was empty.");
    }

    Factory* factory = Registry::FactoryRegistry<Factory>::getFactory(name);

    if (factory == nullptr && !is_optional) {
      ExceptionUtil::throwEnvoyException(
          fmt::format("Didn't find a registered implementation for name: '{}'", name));
    }

    return factory;
  }

  /**
   * Get a Factory from the registry with a particular name (and templated type) with error checking
   * to ensure the name and factory are valid.
   * @param name string identifier for the particular implementation.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory> static Factory& getAndCheckFactoryByName(const std::string& name) {
    return *getAndCheckFactoryByName<Factory>(name, false);
  }

  /**
   * Get a Factory from the registry with a particular name or return nullptr.
   * @param name string identifier for the particular implementation.
   */
  template <class Factory> static Factory* getFactoryByName(const std::string& name) {
    if (name.empty()) {
      return nullptr;
    }

    return Registry::FactoryRegistry<Factory>::getFactory(name);
  }

  /**
   * Get a Factory from the registry or return nullptr.
   * @param message proto that contains fields 'name' and 'typed_config'.
   */
  template <class Factory, class ProtoMessage>
  static Factory* getFactory(const ProtoMessage& message) {
    Factory* factory = Utility::getFactoryByType<Factory>(message.typed_config());
    if (factory != nullptr) {
      return factory;
    }

    return Utility::getFactoryByName<Factory>(message.name());
  }

  /**
   * Get a Factory from the registry with error checking to ensure the name and the factory are
   * valid. And a flag to control return nullptr or throw an exception.
   * @param message proto that contains fields 'name' and 'typed_config'.
   * @param is_optional an exception will be throw when the value is true and no factory found.
   * @return factory the factory requested or nullptr if it does not exist.
   */
  template <class Factory, class ProtoMessage>
  static Factory* getAndCheckFactory(const ProtoMessage& message, bool is_optional) {
    Factory* factory = Utility::getFactoryByType<Factory>(message.typed_config());
    if (factory != nullptr) {
      return factory;
    }

    return Utility::getAndCheckFactoryByName<Factory>(message.name(), is_optional);
  }

  /**
   * Get a Factory from the registry with error checking to ensure the name and the factory are
   * valid.
   * @param message proto that contains fields 'name' and 'typed_config'.
   */
  template <class Factory, class ProtoMessage>
  static Factory& getAndCheckFactory(const ProtoMessage& message) {
    return *getAndCheckFactory<Factory>(message, false);
  }

  /**
   * Get type URL from a typed config.
   * @param typed_config for the extension config.
   */
  static std::string getFactoryType(const ProtobufWkt::Any& typed_config) {
    static const std::string& typed_struct_type =
        udpa::type::v1::TypedStruct::default_instance().GetDescriptor()->full_name();
    // Unpack methods will only use the fully qualified type name after the last '/'.
    // https://github.com/protocolbuffers/protobuf/blob/3.6.x/src/google/protobuf/any.proto#L87
    auto type = std::string(TypeUtil::typeUrlToDescriptorFullName(typed_config.type_url()));
    if (type == typed_struct_type) {
      udpa::type::v1::TypedStruct typed_struct;
      MessageUtil::unpackTo(typed_config, typed_struct);
      // Not handling nested structs or typed structs in typed structs
      return std::string(TypeUtil::typeUrlToDescriptorFullName(typed_struct.type_url()));
    }
    return type;
  }

  /**
   * Get a Factory from the registry by type URL.
   * @param typed_config for the extension config.
   */
  template <class Factory> static Factory* getFactoryByType(const ProtobufWkt::Any& typed_config) {
    if (typed_config.type_url().empty()) {
      return nullptr;
    }
    return Registry::FactoryRegistry<Factory>::getFactoryByType(getFactoryType(typed_config));
  }

  /**
   * Translate a nested config into a proto message provided by the implementation factory.
   * @param enclosing_message proto that contains a field 'typed_config'. Note: the enclosing proto
   * is provided because for statically registered implementations, a custom config is generally
   * optional, which means the conversion must be done conditionally.
   * @param validation_visitor message validation visitor instance.
   * @param factory implementation factory with the method 'createEmptyConfigProto' to produce a
   * proto to be filled with the translated configuration.
   */
  template <class ProtoMessage, class Factory>
  static ProtobufTypes::MessagePtr
  translateToFactoryConfig(const ProtoMessage& enclosing_message,
                           ProtobufMessage::ValidationVisitor& validation_visitor,
                           Factory& factory) {
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

    // Fail in an obvious way if a plugin does not return a proto.
    RELEASE_ASSERT(config != nullptr, "");

    // Check that the config type is not google.protobuf.Empty
    RELEASE_ASSERT(config->GetDescriptor()->full_name() != "google.protobuf.Empty", "");

    translateOpaqueConfig(enclosing_message.typed_config(),
                          enclosing_message.hidden_envoy_deprecated_config(), validation_visitor,
                          *config);
    return config;
  }

  /**
   * Translate the typed any field into a proto message provided by the implementation factory.
   * @param typed_config typed configuration.
   * @param validation_visitor message validation visitor instance.
   * @param factory implementation factory with the method 'createEmptyConfigProto' to produce a
   * proto to be filled with the translated configuration.
   */
  template <class Factory>
  static ProtobufTypes::MessagePtr
  translateAnyToFactoryConfig(const ProtobufWkt::Any& typed_config,
                              ProtobufMessage::ValidationVisitor& validation_visitor,
                              Factory& factory) {
    ProtobufTypes::MessagePtr config = factory.createEmptyConfigProto();

    // Fail in an obvious way if a plugin does not return a proto.
    RELEASE_ASSERT(config != nullptr, "");

    // Check that the config type is not google.protobuf.Empty
    RELEASE_ASSERT(config->GetDescriptor()->full_name() != "google.protobuf.Empty", "");

    translateOpaqueConfig(typed_config, ProtobufWkt::Struct(), validation_visitor, *config);
    return config;
  }

  /**
   * Truncates the message to a length less than default GRPC trailers size limit (by default 8KiB).
   */
  static std::string truncateGrpcStatusMessage(absl::string_view error_message);

  /**
   * Create TagProducer instance. Check all tag names for conflicts to avoid
   * unexpected tag name overwriting.
   * @param bootstrap bootstrap proto.
   * @throws EnvoyException when the conflict of tag names is found.
   */
  static Stats::TagProducerPtr
  createTagProducer(const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

  /**
   * Create StatsMatcher instance.
   */
  static Stats::StatsMatcherPtr
  createStatsMatcher(const envoy::config::bootstrap::v3::Bootstrap& bootstrap,
                     Stats::SymbolTable& symbol_table);

  /**
   * Create HistogramSettings instance.
   */
  static Stats::HistogramSettingsConstPtr
  createHistogramSettings(const envoy::config::bootstrap::v3::Bootstrap& bootstrap);

  /**
   * Obtain gRPC async client factory from a envoy::config::core::v3::ApiConfigSource.
   * @param async_client_manager gRPC async client manager.
   * @param api_config_source envoy::config::core::v3::ApiConfigSource. Must have config type GRPC.
   * @param skip_cluster_check whether to skip cluster validation.
   * @return Grpc::AsyncClientFactoryPtr gRPC async client factory.
   */
  static Grpc::AsyncClientFactoryPtr
  factoryForGrpcApiConfigSource(Grpc::AsyncClientManager& async_client_manager,
                                const envoy::config::core::v3::ApiConfigSource& api_config_source,
                                Stats::Scope& scope, bool skip_cluster_check);

  /**
   * Translate opaque config from google.protobuf.Any or google.protobuf.Struct to defined proto
   * message.
   * @param typed_config opaque config packed in google.protobuf.Any
   * @param config the deprecated google.protobuf.Struct config, empty struct if doesn't exist.
   * @param validation_visitor message validation visitor instance.
   * @param out_proto the proto message instantiated by extensions
   */
  static void translateOpaqueConfig(const ProtobufWkt::Any& typed_config,
                                    const ProtobufWkt::Struct& config,
                                    ProtobufMessage::ValidationVisitor& validation_visitor,
                                    Protobuf::Message& out_proto);

  /**
   * Verify that any filter designed to be terminal is configured to be terminal, and vice versa.
   * @param name the name of the filter.
   * @param filter_type the type of filter.
   * @param filter_chain_type the type of filter chain.
   * @param is_terminal_filter true if the filter is designed to be terminal.
   * @param last_filter_in_current_config true if the filter is last in the configuration.
   * @throws EnvoyException if there is a mismatch between design and configuration.
   */
  static void validateTerminalFilters(const std::string& name, const std::string& filter_type,
                                      const std::string& filter_chain_type, bool is_terminal_filter,
                                      bool last_filter_in_current_config) {
    if (is_terminal_filter && !last_filter_in_current_config) {
      ExceptionUtil::throwEnvoyException(
          fmt::format("Error: terminal filter named {} of type {} must be the "
                      "last filter in a {} filter chain.",
                      name, filter_type, filter_chain_type));
    } else if (!is_terminal_filter && last_filter_in_current_config) {
      ExceptionUtil::throwEnvoyException(fmt::format(
          "Error: non-terminal filter named {} of type {} is the last filter in a {} filter chain.",
          name, filter_type, filter_chain_type));
    }
  }

  /**
   * Prepares the DNS failure refresh backoff strategy given the cluster configuration.
   * @param config the config that contains dns refresh information.
   * @param dns_refresh_rate_ms the default DNS refresh rate.
   * @param random the random generator.
   * @return BackOffStrategyPtr for scheduling refreshes.
   */
  template <typename T>
  static BackOffStrategyPtr prepareDnsRefreshStrategy(const T& config, uint64_t dns_refresh_rate_ms,
                                                      Random::RandomGenerator& random) {
    if (config.has_dns_failure_refresh_rate()) {
      uint64_t base_interval_ms =
          PROTOBUF_GET_MS_REQUIRED(config.dns_failure_refresh_rate(), base_interval);
      uint64_t max_interval_ms = PROTOBUF_GET_MS_OR_DEFAULT(config.dns_failure_refresh_rate(),
                                                            max_interval, base_interval_ms * 10);
      if (max_interval_ms < base_interval_ms) {
        ExceptionUtil::throwEnvoyException(
            "dns_failure_refresh_rate must have max_interval greater than "
            "or equal to the base_interval");
      }
      return std::make_unique<JitteredExponentialBackOffStrategy>(base_interval_ms, max_interval_ms,
                                                                  random);
    }
    return std::make_unique<FixedBackOffStrategy>(dns_refresh_rate_ms);
  }
};

} // namespace Config
} // namespace Envoy
