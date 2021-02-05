#include "common/config/subscription_factory_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"

#include "common/config/filesystem_subscription_impl.h"
#include "common/config/grpc_mux_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/legacy_grpc_mux_impl.h"
#include "common/config/legacy_grpc_subscription_impl.h"
#include "common/config/type_to_endpoint.h"
#include "common/config/utility.h"
#include "common/config/xds_resource.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"

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
    bool use_namespace_matching) {
  Config::Utility::checkLocalInfo(type_url, local_info_);
  std::unique_ptr<Subscription> result;
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
    const auto transport_api_version = Utility::getAndCheckTransportVersion(api_config_source);
    switch (api_config_source.api_type()) {
    case envoy::config::core::v3::ApiConfigSource::hidden_envoy_deprecated_UNSUPPORTED_REST_LEGACY:
      throw EnvoyException(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::config::core::v3::ApiConfigSource::REST:
      return std::make_unique<HttpSubscriptionImpl>(
          local_info_, cm_, api_config_source.cluster_names()[0], dispatcher_,
          api_.randomGenerator(), Utility::apiConfigSourceRefreshDelay(api_config_source),
          Utility::apiConfigSourceRequestTimeout(api_config_source),
          restMethod(type_url, transport_api_version), type_url, transport_api_version, callbacks,
          resource_decoder, stats, Utility::configSourceInitialFetchTimeout(config),
          validation_visitor_);
    case envoy::config::core::v3::ApiConfigSource::GRPC:
      if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.legacy_sotw_xds")) {
        return std::make_unique<LegacyGrpcSubscriptionImpl>(
            std::make_shared<Config::LegacyGrpcMuxImpl>(
                local_info_,
                Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                       api_config_source, scope, true)
                    ->create(),
                dispatcher_, sotwGrpcMethod(type_url, transport_api_version), transport_api_version,
                api_.randomGenerator(), scope, Utility::parseRateLimitSettings(api_config_source),
                api_config_source.set_node_on_first_message_only()),
            callbacks, resource_decoder, stats, type_url, dispatcher_,
            Utility::configSourceInitialFetchTimeout(config),
            /*is_aggregated*/ false, use_namespace_matching);
      }

      return std::make_unique<GrpcSubscriptionImpl>(
          std::make_shared<GrpcMuxSotw>(
              Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                     api_config_source, scope, true)
                  ->create(),
              dispatcher_, sotwGrpcMethod(type_url, transport_api_version), transport_api_version,
              api_.randomGenerator(), scope, Utility::parseRateLimitSettings(api_config_source),
              local_info_, api_config_source.set_node_on_first_message_only()),
          type_url, callbacks, resource_decoder, stats, dispatcher_.timeSource(),
          Utility::configSourceInitialFetchTimeout(config),
          /*is_aggregated*/ false, use_namespace_matching);
    case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
      return std::make_unique<GrpcSubscriptionImpl>(
          std::make_shared<GrpcMuxDelta>(
              Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                             api_config_source, scope, true)
                  ->create(),
              dispatcher_, deltaGrpcMethod(type_url, transport_api_version), transport_api_version,
              api_.randomGenerator(), scope, Utility::parseRateLimitSettings(api_config_source),
              local_info_, api_config_source.set_node_on_first_message_only()),
          type_url, callbacks, resource_decoder, stats, dispatcher_.timeSource(),
          Utility::configSourceInitialFetchTimeout(config), /*is_aggregated*/ false,
          use_namespace_matching);
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
    if (cm_.adsMux()->isLegacy()) {
      return std::make_unique<LegacyGrpcSubscriptionImpl>(
          cm_.adsMux(), callbacks, resource_decoder, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config), true, use_namespace_matching);
    }
    return std::make_unique<GrpcSubscriptionImpl>(
        cm_.adsMux(), type_url, callbacks, resource_decoder, stats, dispatcher_.timeSource(),
        Utility::configSourceInitialFetchTimeout(config), true, use_namespace_matching);
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
  std::unique_ptr<Subscription> result;
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
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                            api_config_source);

    switch (api_config_source.api_type()) {
    case envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC: {
      return std::make_unique<GrpcCollectionSubscriptionImpl>(
          collection_locator, cm_.adsMux(), callbacks, resource_decoder, stats,
          dispatcher_.timeSource(), Utility::configSourceInitialFetchTimeout(config), false);
    }
    default:
      // TODO(htuch): Add support for non-aggregated delta gRPC, but there will always be options,
      // e.g. v2 REST, that don't make sense for xdstp:// ResourceLocators.
      throw EnvoyException(fmt::format("Unknown xdstp:// transport API type in {}",
                                       api_config_source.DebugString()));
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
