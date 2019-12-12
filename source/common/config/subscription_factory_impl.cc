#include "common/config/subscription_factory_impl.h"

#include "common/config/delta_subscription_impl.h"
#include "common/config/filesystem_subscription_impl.h"
#include "common/config/grpc_mux_subscription_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/new_grpc_mux_impl.h"
#include "common/config/type_to_endpoint.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

SubscriptionFactoryImpl::SubscriptionFactoryImpl(
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
    ProtobufMessage::ValidationVisitor& validation_visitor, Api::Api& api)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm), random_(random),
      validation_visitor_(validation_visitor), api_(api) {}

SubscriptionPtr SubscriptionFactoryImpl::subscriptionFromConfigSource(
    const envoy::api::v2::core::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, SubscriptionCallbacks& callbacks) {
  Config::Utility::checkLocalInfo(type_url, local_info_);
  std::unique_ptr<Subscription> result;
  SubscriptionStats stats = Utility::generateStats(scope);
  switch (config.config_source_specifier_case()) {
  case envoy::api::v2::core::ConfigSource::kPath: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path(), api_);
    result = std::make_unique<Config::FilesystemSubscriptionImpl>(
        dispatcher_, config.path(), callbacks, stats, validation_visitor_, api_);
    break;
  }
  case envoy::api::v2::core::ConfigSource::kApiConfigSource: {
    const envoy::api::v2::core::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.clusters(), api_config_source);
    switch (api_config_source.api_type()) {
    case envoy::api::v2::core::ApiConfigSource::UNSUPPORTED_REST_LEGACY:
      throw EnvoyException(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::api::v2::core::ApiConfigSource::REST:
      result = std::make_unique<HttpSubscriptionImpl>(
          local_info_, cm_, api_config_source.cluster_names()[0], dispatcher_, random_,
          Utility::apiConfigSourceRefreshDelay(api_config_source),
          Utility::apiConfigSourceRequestTimeout(api_config_source), restMethod(type_url),
          callbacks, stats, Utility::configSourceInitialFetchTimeout(config), validation_visitor_);
      break;
    case envoy::api::v2::core::ApiConfigSource::GRPC:
      result = std::make_unique<GrpcSubscriptionImpl>(
          local_info_,
          Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                         api_config_source, scope)
              ->create(),
          dispatcher_, random_, sotwGrpcMethod(type_url), type_url, callbacks, stats, scope,
          Utility::parseRateLimitSettings(api_config_source),
          Utility::configSourceInitialFetchTimeout(config),
          api_config_source.set_node_on_first_message_only());
      break;
    case envoy::api::v2::core::ApiConfigSource::DELTA_GRPC: {
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cm_.clusters(), api_config_source);
      result = std::make_unique<DeltaSubscriptionImpl>(
          std::make_shared<Config::NewGrpcMuxImpl>(
              Config::Utility::factoryForGrpcApiConfigSource(cm_.grpcAsyncClientManager(),
                                                             api_config_source, scope)
                  ->create(),
              dispatcher_, deltaGrpcMethod(type_url), random_, scope,
              Utility::parseRateLimitSettings(api_config_source), local_info_),
          type_url, callbacks, stats, Utility::configSourceInitialFetchTimeout(config), false);
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    break;
  }
  case envoy::api::v2::core::ConfigSource::kAds: {
    if (cm_.adsMux()->isDelta()) {
      result = std::make_unique<DeltaSubscriptionImpl>(
          cm_.adsMux(), type_url, callbacks, stats,
          Utility::configSourceInitialFetchTimeout(config), true);
    } else {
      result = std::make_unique<GrpcMuxSubscriptionImpl>(
          cm_.adsMux(), callbacks, stats, type_url, dispatcher_,
          Utility::configSourceInitialFetchTimeout(config));
    }
    break;
  }
  default:
    throw EnvoyException("Missing config source specifier in envoy::api::v2::core::ConfigSource");
  }
  return result;
}

} // namespace Config
} // namespace Envoy
