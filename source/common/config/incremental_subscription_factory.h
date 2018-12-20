#pragma once

#include <functional>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/config/incremental_subscription.h"
#include "envoy/stats/scope.h"
#include "envoy/upstream/cluster_manager.h"

#include "common/config/incremental_subscription_impl.h"
#include "common/config/utility.h"
#include "common/filesystem/filesystem_impl.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Config {

class IncrementalSubscriptionFactory {
public:
  /**
   * Subscription factory.
   * @param config envoy::api::v2::core::ConfigSource to construct from.
   * @param local_info LocalInfo::LocalInfo local info.
   * @param dispatcher event dispatcher.
   * @param cm cluster manager for async clients.
   * @param scope stats scope.
   * @param grpc_method fully qualified name of v2 gRPC API bidi streaming method (as per protobuf
   *        service description).
   */
  template <class ResourceType>
  static std::unique_ptr<IncrementalSubscription<ResourceType>> subscriptionFromConfigSource(
      const envoy::api::v2::core::ConfigSource& config, const LocalInfo::LocalInfo& local_info,
      Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
      Stats::Scope& scope, const std::string& grpc_method) {

    if (config.config_source_specifier_case() !=
        envoy::api::v2::core::ConfigSource::kApiConfigSource) {
      throw EnvoyException(
          "envoy::api::v2::core::ConfigSource specifier must be kApiConfigSource.");
    }
// TODO should be reduntant if we do add this enum value    if (config.api_config_source().api_type() != envoy::api::v2::core::ApiConfigSource::INCREMENTAL_GRPC) {
//      throw EnvoyException("Incremental xDS must use gRPC.");
//    }
    std::cerr<<"in subscriptionFromConfigSource. passed initial checks."<<std::endl;

    IncrementalSubscriptionStats stats = Utility::generateIncrementalStats(scope);
    ControlPlaneStats control_plane_stats = Utility::generateControlPlaneStats(scope);
    Utility::checkApiConfigSourceSubscriptionBackingCluster(cm.clusters(),
                                                            config.api_config_source());
    std::cerr<<"in subscriptionFromConfigSource. passed other checks."<<std::endl;
    return std::make_unique<IncrementalSubscriptionImpl<ResourceType>>(
        local_info,
        Config::Utility::factoryForGrpcApiConfigSource(cm.grpcAsyncClientManager(),
                                                       config.api_config_source(), scope)
            ->create(),
        dispatcher, *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method),
        random, Utility::parseRateLimitSettings(config.api_config_source()), stats,
        control_plane_stats);
  }
};

} // namespace Config
} // namespace Envoy
