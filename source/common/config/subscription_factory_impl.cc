#include "source/common/config/subscription_factory_impl.h"

#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/config/xds_resources_delegate.h"

#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/type_to_endpoint.h"
#include "source/common/config/utility.h"
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
    XdsResourcesDelegateOptRef xds_resources_delegate, XdsConfigTrackerOptRef xds_config_tracker)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm),
      validation_visitor_(validation_visitor), api_(api), server_(server),
      xds_resources_delegate_(xds_resources_delegate), xds_config_tracker_(xds_config_tracker) {}

absl::StatusOr<SubscriptionPtr> SubscriptionFactoryImpl::subscriptionFromConfigSource(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoderSharedPtr resource_decoder, const SubscriptionOptions& options) {
  RETURN_IF_NOT_OK(Config::Utility::checkLocalInfo(type_url, local_info_));
  SubscriptionStats stats = Utility::generateStats(scope);

  std::string subscription_type = "";
  ConfigSubscriptionFactory::SubscriptionData data{local_info_,
                                                   dispatcher_,
                                                   cm_,
                                                   validation_visitor_,
                                                   api_,
                                                   server_,
                                                   xds_resources_delegate_,
                                                   xds_config_tracker_,
                                                   config,
                                                   type_url,
                                                   scope,
                                                   callbacks,
                                                   resource_decoder,
                                                   options,
                                                   absl::nullopt,
                                                   stats};

  switch (config.config_source_specifier_case()) {
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPath: {
    RETURN_IF_NOT_OK(Utility::checkFilesystemSubscriptionBackingPath(config.path(), api_));
    subscription_type = "envoy.config_subscription.filesystem";
    break;
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kPathConfigSource: {
    RETURN_IF_NOT_OK(
        Utility::checkFilesystemSubscriptionBackingPath(config.path_config_source().path(), api_));
    subscription_type = "envoy.config_subscription.filesystem";
    break;
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    RETURN_IF_NOT_OK(Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.primaryClusters(),
                                                                             api_config_source));
    RETURN_IF_NOT_OK(Utility::checkTransportVersion(api_config_source));
    switch (api_config_source.api_type()) {
      PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
    case envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC:
      return absl::InvalidArgumentError("Unsupported config source AGGREGATED_GRPC");
    case envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC:
      return absl::InvalidArgumentError("Unsupported config source AGGREGATED_DELTA_GRPC");
    case envoy::config::core::v3::ApiConfigSource::DEPRECATED_AND_UNAVAILABLE_DO_NOT_USE:
      return absl::InvalidArgumentError(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::config::core::v3::ApiConfigSource::REST:
      subscription_type = "envoy.config_subscription.rest";
      break;
    case envoy::config::core::v3::ApiConfigSource::GRPC:
      subscription_type = "envoy.config_subscription.grpc";
      break;
    case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC:
      subscription_type = "envoy.config_subscription.delta_grpc";
      break;
    }
    if (subscription_type.empty()) {
      return absl::InvalidArgumentError("Invalid API config source API type");
    }
    break;
  }
  case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
    subscription_type = "envoy.config_subscription.ads";
    break;
  }
  default:
    return absl::InvalidArgumentError(
        "Missing config source specifier in envoy::config::core::v3::ConfigSource");
  }
  ConfigSubscriptionFactory* factory =
      Registry::FactoryRegistry<ConfigSubscriptionFactory>::getFactory(subscription_type);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Didn't find a registered config subscription factory implementation for name: '{}'",
        subscription_type));
  }
  return factory->create(data);
}

absl::StatusOr<SubscriptionPtr> createFromFactory(ConfigSubscriptionFactory::SubscriptionData& data,
                                                  absl::string_view subscription_type) {
  ConfigSubscriptionFactory* factory =
      Registry::FactoryRegistry<ConfigSubscriptionFactory>::getFactory(subscription_type);
  if (factory == nullptr) {
    return absl::InvalidArgumentError(fmt::format(
        "Didn't find a registered config subscription factory implementation for name: '{}'",
        subscription_type));
  }
  return factory->create(data);
}

absl::StatusOr<SubscriptionPtr> SubscriptionFactoryImpl::collectionSubscriptionFromUrl(
    const xds::core::v3::ResourceLocator& collection_locator,
    const envoy::config::core::v3::ConfigSource& config, absl::string_view resource_type,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks,
    OpaqueResourceDecoderSharedPtr resource_decoder) {
  SubscriptionStats stats = Utility::generateStats(scope);
  SubscriptionOptions options;
  envoy::config::core::v3::ConfigSource factory_config = config;
  ConfigSubscriptionFactory::SubscriptionData data{local_info_,
                                                   dispatcher_,
                                                   cm_,
                                                   validation_visitor_,
                                                   api_,
                                                   server_,
                                                   xds_resources_delegate_,
                                                   xds_config_tracker_,
                                                   factory_config,
                                                   "",
                                                   scope,
                                                   callbacks,
                                                   resource_decoder,
                                                   options,
                                                   {collection_locator},
                                                   stats};
  switch (collection_locator.scheme()) {
  case xds::core::v3::ResourceLocator::FILE: {
    const std::string path = Http::Utility::localPathFromFilePath(collection_locator.id());
    RETURN_IF_NOT_OK(Utility::checkFilesystemSubscriptionBackingPath(path, api_));
    factory_config.set_path(path);
    auto ptr_or_error = createFromFactory(data, "envoy.config_subscription.filesystem_collection");
    RETURN_IF_NOT_OK(ptr_or_error.status());
    return std::move(ptr_or_error.value());
  }
  case xds::core::v3::ResourceLocator::XDSTP: {
    if (resource_type != collection_locator.resource_type()) {
      return absl::InvalidArgumentError(
          fmt::format("xdstp:// type does not match {} in {}", resource_type,
                      Config::XdsResourceIdentifier::encodeUrl(collection_locator)));
    }
    switch (config.config_source_specifier_case()) {
    case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kApiConfigSource: {
      const envoy::config::core::v3::ApiConfigSource& api_config_source =
          config.api_config_source();
      RETURN_IF_NOT_OK(Utility::checkApiConfigSourceSubscriptionBackingCluster(
          cm_.primaryClusters(), api_config_source));
      // All Envoy collections currently are xDS resource graph roots and require node context
      // parameters.
      options.add_xdstp_node_context_params_ = true;
      switch (api_config_source.api_type()) {
      case envoy::config::core::v3::ApiConfigSource::DELTA_GRPC: {
        std::string type_url = TypeUtil::descriptorFullNameToTypeUrl(resource_type);
        data.type_url_ = type_url;
        auto ptr_or_error =
            createFromFactory(data, "envoy.config_subscription.delta_grpc_collection");
        RETURN_IF_NOT_OK(ptr_or_error.status());
        return std::move(ptr_or_error.value());
      }
      case envoy::config::core::v3::ApiConfigSource::AGGREGATED_GRPC:
        FALLTHRU;
      case envoy::config::core::v3::ApiConfigSource::AGGREGATED_DELTA_GRPC: {
        auto ptr_or_error =
            createFromFactory(data, "envoy.config_subscription.aggregated_grpc_collection");
        RETURN_IF_NOT_OK(ptr_or_error.status());
        return std::move(ptr_or_error.value());
      }
      default:
        return absl::InvalidArgumentError(fmt::format("Unknown xdstp:// transport API type in {}",
                                                      api_config_source.DebugString()));
      }
    }
    case envoy::config::core::v3::ConfigSource::ConfigSourceSpecifierCase::kAds: {
      // TODO(adisuissa): verify that the ADS is set up in delta-xDS mode.
      // All Envoy collections currently are xDS resource graph roots and require node context
      // parameters.
      options.add_xdstp_node_context_params_ = true;
      auto ptr_or_error = createFromFactory(data, "envoy.config_subscription.ads_collection");
      RETURN_IF_NOT_OK(ptr_or_error.status());
      return std::move(ptr_or_error.value());
    }
    default:
      return absl::InvalidArgumentError(
          "Missing or not supported config source specifier in "
          "envoy::config::core::v3::ConfigSource for a collection. Only ADS and "
          "gRPC in delta-xDS mode are supported.");
    }
  }
  default:
    // TODO(htuch): Implement HTTP semantics for collection ResourceLocators.
    return absl::InvalidArgumentError("Unsupported code path");
  }
}

} // namespace Config
} // namespace Envoy
