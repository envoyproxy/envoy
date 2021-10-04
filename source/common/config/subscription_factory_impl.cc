#include "source/common/config/subscription_factory_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"

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
    Api::Api& api)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm),
      validation_visitor_(validation_visitor), api_(api) {}

SubscriptionPtr SubscriptionFactoryImpl::subscriptionFromConfigSource(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks, OpaqueResourceDecoder& resource_decoder,
    const SubscriptionOptions& options) {
  Config::Utility::checkLocalInfo(type_url, local_info_);
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (config.config_source_specifier_case()) {
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path(), api_);
    return std::make_unique<Config::FilesystemSubscriptionImpl>(
        dispatcher_, config.path(), callbacks, resource_decoder, stats, validation_visitor_, api_);
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                            api_config_source);
    Utility::checkTransportVersion(api_config_source);
    switch (api_config_source.api_type()) {
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
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        mux = std::make_shared<Config::XdsMux::GrpcMuxSotw>(
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, sotwGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_,
            api_config_source.set_node_on_first_message_only());
      } else {
        mux = std::make_shared<Config::GrpcMuxImpl>(
            local_info_,
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, sotwGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source),
            api_config_source.set_node_on_first_message_only());
      }
      return std::make_unique<GrpcSubscriptionImpl>(
          std::move(mux), callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config),
          /*is_aggregated*/ false, options);
    }
    case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
      GrpcMuxSharedPtr mux;
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_mux")) {
        mux = std::make_shared<Config::XdsMux::GrpcMuxDelta>(
            Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(), api_config_source,
                                                   scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, deltaGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_,
            api_config_source.set_node_on_first_message_only());
      } else {
        mux = std::make_shared<Config::NewGrpcMuxImpl>(
            Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                           api_config_source, scope, true)
                ->createUncachedRawAsyncClient(),
            dispatcher_, deltaGrpcMethod(type_url), api_.randomGenerator(), scope,
            Utility::parseRateLimitSettings(api_config_source), local_info_);
      }
      return std::make_unique<GrpcSubscriptionImpl>(
          std::move(mux), callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config), /*is_aggregated*/ false, options);
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
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
  NOT_REACHED_GCOVR_EXCL_LINE;
}

SubscriptionPtr SubscriptionFactoryImpl::collectionSubscriptionFromUrl(
    const xds::core::v3::ResourceLocator& collection_locator,
    const envoy::config::core::v3::ConfigSource& config, absl::string_view resource_type,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoder& resource_decoder) {
  SubscriptionStats stats = Utility::generateStats(scope);

  switch (collection_locator.scheme()) {
  case xds::core::v3::ResourceLocator::FILE: {
    const std::string path = Http::Utility::localPathFromFilePath(collection_locator.id());
    Utility::checkFilesystemSubscriptionBackingPath(path, api_);
    return std::make_unique<Config::FilesystemCollectionSubscriptionImpl>(
        dispatcher_, path, callbacks, resource_decoder, stats, validation_visitor_, api_);
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
                Utility::parseRateLimitSettings(api_config_source), local_info_),
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
    NOT_IMPLEMENTED_GCOVR_EXCL_LINE;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

} // namespace Config
} // namespace Envoy
