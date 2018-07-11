#pragma once

#include <functional>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/subscription.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/filesystem_subscription_impl.h"
#include "common/config/grpc_mux_subscription_impl.h"
#include "common/config/grpc_subscription_impl.h"
#include "common/config/http_subscription_impl.h"
#include "common/config/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class SubscriptionFactory {
public:
  /**
   * Subscription factory.
   * @param config envoy::api::v2::core::ConfigSource to construct from.
   * @param node envoy::api::v2::core::Node identifier.
   * @param dispatcher event dispatcher.
   * @param cm cluster manager for async clients (when REST/gRPC).
   * @param random random generator for jittering polling delays (when REST).
   * @param scope stats scope.
   * @param rest_legacy_constructor constructor function for Subscription adapters (when legacy v1
   * REST).
   * @param rest_method fully qualified name of v2 REST API method (as per protobuf service
   *        description).
   * @param grpc_method fully qualified name of v2 gRPC API bidi streaming method (as per protobuf
   *        service description).
   */
  template <class ResourceType>
  static std::unique_ptr<Subscription<ResourceType>> subscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& config, const envoy::api::v2::core::Node& node,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope, std::function<Subscription<ResourceType>*()> rest_legacy_constructor,
      const std::string& rest_method, const std::string& grpc_method) {
    std::unique_ptr<Subscription<ResourceType>> result;
    SubscriptionStats stats = Utility::generateStats(scope);
    switch (config.config_source_specifier_case()) {
    case envoy::api::v2::core::ConfigSource::kPath: {
      Utility::checkFilesystemSubscriptionBackingPath(config.path());
      result.reset(
          new Config::FilesystemSubscriptionImpl<ResourceType>(dispatcher, config.path(), stats));
      break;
    }
    case envoy::api::v2::core::ConfigSource::kApiConfigSource: {
      const envoy::api::v2::core::ApiConfigSource& api_config_source = config.api_config_source();
      Utility::checkApiConfigSourceSubscriptionBackingCluster(cm.clusters(), api_config_source);
      switch (api_config_source.api_type()) {
      case envoy::api::v2::core::ApiConfigSource::REST_LEGACY:
        result.reset(rest_legacy_constructor());
        break;
      case envoy::api::v2::core::ApiConfigSource::REST:
        result.reset(new HttpSubscriptionImpl<ResourceType>(
            node, cm, api_config_source.cluster_names()[0], dispatcher, random,
            Utility::apiConfigSourceRefreshDelay(api_config_source),
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(rest_method), stats));
        break;
      case envoy::api::v2::core::ApiConfigSource::GRPC: {
        result.reset(new GrpcSubscriptionImpl<ResourceType>(
            node,
            Config::Utility::factoryForGrpcApiConfigSource(cm.grpcAsyncClientManager(),
                                                           config.api_config_source(), scope)
                ->create(),
            dispatcher, random,
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method), stats));
        break;
      }
      default:
        NOT_REACHED;
      }
      break;
    }
    case envoy::api::v2::core::ConfigSource::kAds: {
      result.reset(new GrpcMuxSubscriptionImpl<ResourceType>(cm.adsMux(), stats));
      break;
    }
    default:
      throw EnvoyException("Missing config source specifier in envoy::api::v2::core::ConfigSource");
    }
    return result;
  }
};

} // namespace Config
} // namespace Envoy
