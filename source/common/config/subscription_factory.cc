#include "common/config/subscription_factory.h"

#include "common/config/delta_subscription_impl.h"
#include "common/config/filesystem_subscription_impl.h"
#include "common/config/grpc_mux_subscription_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

std::unique_ptr<Subscription> SubscriptionFactory::subscriptionFromConfigSource(
    const envoy::api::v2::core::ConfigSource& config, const LocalInfo::LocalInfo& local_info,
    Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
    Stats::Scope& scope, const std::string& rest_method, const std::string& grpc_method,
    absl::string_view type_url, Api::Api& api) {
  std::unique_ptr<Subscription> result;
  SubscriptionStats stats = Utility::generateStats(scope);
  switch (config.config_source_specifier_case()) {
  case envoy::api::v2::core::ConfigSource::kPath: {
    Utility::checkFilesystemSubscriptionBackingPath(config.path(), api);
    result =
        std::make_unique<Config::FilesystemSubscriptionImpl>(dispatcher, config.path(), stats, api);
    break;
  }
  case envoy::api::v2::core::ConfigSource::kApiConfigSource: {
    const envoy::api::v2::core::ApiConfigSource& api_config_source = config.api_config_source();
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm.clusters(), api_config_source);
    switch (api_config_source.api_type()) {
    case envoy::api::v2::core::ApiConfigSource::UNSUPPORTED_REST_LEGACY:
      throw EnvoyException(
          "REST_LEGACY no longer a supported ApiConfigSource. "
          "Please specify an explicit supported api_type in the following config:\n" +
          config.DebugString());
    case envoy::api::v2::core::ApiConfigSource::REST:
      result = std::make_unique<HttpSubscriptionImpl>(
          local_info, cm, api_config_source.cluster_names()[0], dispatcher, random,
          Utility::apiConfigSourceRefreshDelay(api_config_source),
          Utility::apiConfigSourceRequestTimeout(api_config_source),
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(rest_method), stats,
          Utility::configSourceInitialFetchTimeout(config));
      break;
    case envoy::api::v2::core::ApiConfigSource::GRPC:
      result = std::make_unique<GrpcSubscriptionImpl>(
          local_info,
          Config::Utility::factoryForGrpcApiConfigSource(cm.grpcAsyncClientManager(),
                                                         api_config_source, scope)
              ->create(),
          dispatcher, random,
          *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method), type_url,
          stats, scope, Utility::parseRateLimitSettings(api_config_source),
          Utility::configSourceInitialFetchTimeout(config));
      break;
    case envoy::api::v2::core::ApiConfigSource::DELTA_GRPC: {
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cm.clusters(), api_config_source);
      result = std::make_unique<DeltaSubscriptionImpl>(
          local_info,
          Config::Utility::factoryForGrpcApiConfigSource(cm.grpcAsyncClientManager(),
                                                         api_config_source, scope)
              ->create(),
          dispatcher, *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method),
          type_url, random, scope, Utility::parseRateLimitSettings(api_config_source), stats,
          Utility::configSourceInitialFetchTimeout(config));
      break;
    }
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
    break;
  }
  case envoy::api::v2::core::ConfigSource::kAds: {
    result = std::make_unique<GrpcMuxSubscriptionImpl>(
        cm.adsMux(), stats, type_url, dispatcher, Utility::configSourceInitialFetchTimeout(config));
    break;
  }
  default:
    throw EnvoyException("Missing config source specifier in envoy::api::v2::core::ConfigSource");
  }
  return result;
}

} // namespace Config
} // namespace Envoy
