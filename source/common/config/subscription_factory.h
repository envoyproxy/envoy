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
#include "common/upstream/cds_subscription.h"

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
    return subscriptionFromConfigSource(config, node, dispatcher, cm, random,
        scope, rest_legacy_constructor, cm.adsMux(), rest_method, grpc_method);
  }

  template <class ResourceType>
  static std::unique_ptr<Subscription<ResourceType>> subscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& config, const envoy::api::v2::core::Node& node,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope, std::function<Subscription<ResourceType>*()> rest_legacy_constructor,
      Config::GrpcMux& ads_mux,
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
      const std::string& cluster_name = api_config_source.cluster_names()[0];
      switch (api_config_source.api_type()) {
      case envoy::api::v2::core::ApiConfigSource::REST_LEGACY:
        result.reset(rest_legacy_constructor());
        break;
      case envoy::api::v2::core::ApiConfigSource::REST:
        result.reset(new HttpSubscriptionImpl<ResourceType>(
            node, cm, cluster_name, dispatcher, random,
            Utility::apiConfigSourceRefreshDelay(api_config_source),
            *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(rest_method), stats));
        break;
      case envoy::api::v2::core::ApiConfigSource::GRPC: {
        result.reset(new GrpcSubscriptionImpl<ResourceType>(
            node,
            Config::Utility::factoryForApiConfigSource(cm.grpcAsyncClientManager(),
                                                       config.api_config_source(), scope)
                ->create(),
            dispatcher, *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method),
            stats));
        break;
      }
      default:
        NOT_REACHED;
      }
      break;
    }
    case envoy::api::v2::core::ConfigSource::kAds: {
      result.reset(new GrpcMuxSubscriptionImpl<ResourceType>(ads_mux, stats));
      break;
    }
    default:
      throw EnvoyException("Missing config source specifier in envoy::api::v2::core::ConfigSource");
    }
    return result;
  }

  static std::unique_ptr<Subscription<envoy::api::v2::Cluster>> cdsSubscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& cds_config, const Optional<envoy::api::v2::core::ConfigSource>& eds_config,
      const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope, Config::GrpcMux& ads_mux) {

    return subscriptionFromConfigSource<envoy::api::v2::Cluster>(
          cds_config, local_info.node(), dispatcher, cm, random, scope,
          [&cds_config, &eds_config, &cm, &dispatcher, &random, &scope,
           &local_info]() -> Config::Subscription<envoy::api::v2::Cluster>* {
            return new Upstream::CdsSubscription(Config::Utility::generateStats(scope), cds_config,
                                       eds_config, cm, dispatcher, random, local_info);
          },
          ads_mux,
          "envoy.api.v2.ClusterDiscoveryService.FetchClusters",
          "envoy.api.v2.ClusterDiscoveryService.StreamClusters");

  }
};

} // namespace Config
} // namespace Envoy
