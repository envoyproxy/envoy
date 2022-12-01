#include "source/common/config/subscription_factory_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/xds_resources_delegate.h"

#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/filesystem_subscription_impl.h"
#include "source/common/config/grpc_mux_impl.h"
#include "source/common/config/grpc_subscription_impl.h"
#include "source/common/config/http_subscription_impl.h"
#include "source/common/config/new_grpc_mux_impl.h"
#include "source/common/config/type_to_endpoint.h"
#include "source/common/config/utility.h"
#include "source/common/config/xds_mux/grpc_mux_impl.h"
#include "source/common/config/xds_resource.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Config {

SubscriptionFactoryImpl::SubscriptionFactoryImpl(
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    Api::Api& api, const Server::Instance& server,
    XdsResourcesDelegateOptRef xds_resources_delegate)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm),
      validation_visitor_(validation_visitor), api_(api), server_(server),
      xds_resources_delegate_(xds_resources_delegate) {}

SubscriptionPtr SubscriptionFactoryImpl::subscriptionFromConfigSource(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoderSharedPtr resource_decoder, const SubscriptionOptions& options) {
  Config::Utility::checkLocalInfo(type_url, local_info_);
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (config.config_source_specifier_case()) {
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path(), api_);
    return std::make_unique<Config::FilesystemSubscriptionImpl>(
        dispatcher_, makePathConfigSource(config.path()), callbacks, resource_decoder, stats,
        validation_visitor_, api_);
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPathConfigSource: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path_config_source().path(), api_);
    return std::make_unique<Config::FilesystemSubscriptionImpl>(
        dispatcher_, config.path_config_source(), callbacks, resource_decoder, stats,
        validation_visitor_, api_);
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                            api_config_source);
    Utility::checkTransportVersion(api_config_source);
    switch (api_config_source.api_type()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC:
      throw EnvoyException("Unsupported config source AGGREGATED_GRPC");
    case envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC:
      throw EnvoyException("Unsupported config source AGGREGATED_DELTA_GRPC");
    case envoy::config::core::v3::ApiConfigSource::DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE:
      throw EnvoyException(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::config::core::v3::ApiConfigSource::REST:
      return std::make_unique<HttpSubscriptionImpl>(
          local_info_, cm_, api_config_source.cluster_names()[0], dispatcher_,
          api_.randomGenerator(), Utility::apiConfigSourceRefreshDelay(api_config_source),
          Utility::apiConfigSourceRequestTimeout(api_config_source), restMethod(type_url), type_url,
          callbacks, resource_decoder, stats, Utility::configSourceInitialFetchTimeout(config),
          validation_visitor_);
    case envoy::config::core::v3::ApiConfigSource::GRPC: {
      GrpcMuxSharedPtr mux;
      CustomConfigValidatorsPtr custom_config_validators =
          std::make_unique<CustomConfigValidatorsImpl>(validation_visitor_, server_,
                                                       api_config_source.config_validators());
      const std::string control_plane_id =
          Utility::getGrpcControlPlane(api_config_source).value_or("");

      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        mux = std::make_shared<Config::XdsMux::GrpcMuxSotw>(
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, sotwGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_,
            api_config_source.set_node_on_first_message_only(), std::move(custom_config_validators),
            xds_resources_delegate_, control_plane_id);
      } else {
        mux = std::make_shared<Config::GrpcMuxImpl>(
            local_info_,
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, sotwGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source),
            api_config_source.set_node_on_first_message_only(), std::move(custom_config_validators),
            xds_resources_delegate_, control_plane_id);
      }
      return std::make_unique<GrpcSubscriptionImpl>(
          std::move(mux), callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config),
          /*is_aggregated*/ false, options);
    }
    case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
      GrpcMuxSharedPtr mux;
      CustomConfigValidatorsPtr custom_config_validators =
          std::make_unique<CustomConfigValidatorsImpl>(validation_visitor_, server_,
                                                       api_config_source.config_validators());
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        mux = std::make_shared<Config::XdsMux::GrpcMuxDelta>(
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, deltaGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_,
            api_config_source.set_node_on_first_message_only(),
            std::move(custom_config_validators));
      } else {
        mux = std::make_shared<Config::NewGrpcMuxImpl>(
            Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                           api_config_source, scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, deltaGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_,
            std::move(custom_config_validators));
      }
      return std::make_unique<GrpcSubscriptionImpl>(
          std::move(mux), callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config), /*is_aggregated*/ false, options);
    }
    }
    throw EnvoyException("Invalid API config source API type");
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
    return std::make_unique<GrpcSubscriptionImpl>(
        cm_.adsMux(), callbacks, resource_decoder, stats, type_url, dispatcher_,
        Utility::configSourceInitialFetchTimeout(config), true, options);
  }
  default:
    throw EnvoyException(
        "Missing config source specifier in envoy::config::core::v3::ConfigSource");
  }
}

SubscriptionPtr SubscriptionFactoryImpl::collectionSubscriptionFromUrl(
    const xds::core::v3::ResourceLocator& collection_locator,
    const envoy::config::core::v3::ConfigSource& config, absl::string_view resource_type,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoderSharedPtr resource_decoder) {
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (collection_locator.scheme()) {
  case xds::core::v3::ResourceLocator::FILE: {
    const std::string path = Http::Utility::localPathFromFilePath(collection_locator.id());
    Utility::checkFilesystemSubscriptionBackingPath(path, api_);
    return std::make_unique<Config::FilesystemCollectionSubscriptionImpl>(
        dispatcher_, makePathConfigSource(path), callbacks, resource_decoder, stats,
        validation_visitor_, api_);
  }
  case xds::core::v3::ResourceLocator::XDSTP: {
    if (resource_type != collection_locator.resource_type()) {
      throw EnvoyException(
          fmt::format("xdstp:// type does not match {} in {}", resource_type,
                      Config::XdsResourceIdentifier::encodeUrl(collection_locator)));
    }
    switch (config.config_source_specifier_case()) {
    case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
      const envoy::config::core::v3::ApiConfigSource& api_config_source =
          config.api_config_source();
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                              api_config_source);
      CustomConfigValidatorsPtr custom_config_validators =
          std::make_unique<CustomConfigValidatorsImpl>(validation_visitor_, server_,
                                                       api_config_source.config_validators());

      SubscriptionOptions options;
      // All Envoy collections currently are xDS resource graph roots and require node context
      // parameters.
      options.add_xdstp_node_context_params_ = true;
      switch (api_config_source.api_type()) {
      case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
        const std::string type_url = TypeUtil::descriptorFullNameToTypeUrl(resource_type);
        return std::make_unique<GrpcCollectionSubscriptionImpl>(
            collection_locator,
            std::make_shared<Config::NewGrpcMuxImpl>(
                Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                               api_config_source, scope, true)
                    ->createUncachedRawAsyncClient(),
                dispatcher_, deltaGrpcMethod(type_url), api_.randomGenerator(), scope,
                Utility::parseRateLimitSettings(api_config_source), local_info_,
                std::move(custom_config_validators)),
            callbacks, resource_decoder, stats, dispatcher_,
            Utility::configSourceInitialFetchTimeout(config), false, options);
      }
      case envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC: {
        return std::make_unique<GrpcCollectionSubscriptionImpl>(
            collection_locator, cm_.adsMux(), callbacks, resource_decoder, stats, dispatcher_,
            Utility::configSourceInitialFetchTimeout(config), false, options);
      }
      default:
        throw EnvoyException(fmt::format("Unknown xdstp:// transport API type in {}",
                                         api_config_source.DebugString()));
      }
    }
    case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
      // TODO(adisuissa): verify that the ADS is set up in delta-xDS mode.
      SubscriptionOptions options;
      // All Envoy collections currently are xDS resource graph roots and require node context
      // parameters.
      options.add_xdstp_node_context_params_ = true;
      return std::make_unique<GrpcCollectionSubscriptionImpl>(
          collection_locator, cm_.adsMux(), callbacks, resource_decoder, stats, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config), true, options);
    }
    default:
      throw EnvoyException("Missing or not supported config source specifier in "
                           "envoy::config::core::v3::ConfigSource for a collection. Only ADS and "
                           "gRPC in delta-xDS mode are supported.");
    }
  }
  default:
    // TODO(htuch): Implement HTTP semantics for collection ResourceLocators.
    throw EnvoyException("Unsupported code path");
  }
}

} // namespace Config
} // namespace Envoy
