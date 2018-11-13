#include "common/upstream/eds_subscription_factory.h"

namespace Envoy {
namespace Upstream {
Config::GrpcMux& EdsSubscriptionFactory::getOrCreateMux(
    const LocalInfo::LocalInfo& local_info, Grpc::AsyncClientPtr async_client,
    Event::Dispatcher& dispatcher, const Protobuf::MethodDescriptor& service_method,
    Runtime::RandomGenerator& random, const ::envoy::api::v2::core::ApiConfigSource& config_source,
    Stats::Scope& scope, const Config::RateLimitSettings& rate_limit_settings) {
  const uint64_t mux_key = MessageUtil::hash(config_source.grpc_services(0));
  if (muxes_.find(mux_key) == muxes_.end()) {
    muxes_.emplace(std::make_pair(
        mux_key,
        std::make_unique<Config::GrpcMuxImpl>(local_info, std::move(async_client), dispatcher,
                                              service_method, random, scope, rate_limit_settings)));
  }
  return *(muxes_.at(mux_key));
}

std::unique_ptr<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>>
EdsSubscriptionFactory::subscriptionFromConfigSource(
    const envoy::api::v2::core::ConfigSource& config, const LocalInfo::LocalInfo& local_info,
    Event::Dispatcher& dispatcher, Upstream::ClusterManager& cm, Runtime::RandomGenerator& random,
    Stats::Scope& scope,
    std::function<Config::Subscription<envoy::api::v2::ClusterLoadAssignment>*()>
        rest_legacy_constructor,
    const std::string& rest_method, const std::string& grpc_method) {
  if (config.config_source_specifier_case() ==
          envoy::api::v2::core::ConfigSource::kApiConfigSource &&
      config.api_config_source().api_type() == envoy::api::v2::core::ApiConfigSource::GRPC) {
    const envoy::api::v2::core::ApiConfigSource& api_config_source = config.api_config_source();

    Config::GrpcMux& mux_to_use = getOrCreateMux(
        local_info,
        Config::Utility::factoryForGrpcApiConfigSource(cm.grpcAsyncClientManager(),
                                                       api_config_source, scope)
            ->create(),
        dispatcher, *Protobuf::DescriptorPool::generated_pool()->FindMethodByName(grpc_method),
        random, api_config_source, scope,
        Config::Utility::parseRateLimitSettings(api_config_source));

    Config::SubscriptionStats stats = Config::Utility::generateStats(scope);
    return std::make_unique<
        Config::GrpcManagedMuxSubscriptionImpl<envoy::api::v2::ClusterLoadAssignment>>(mux_to_use,
                                                                                       stats);
  }

  return Config::SubscriptionFactory::subscriptionFromConfigSource<
      envoy::api::v2::ClusterLoadAssignment>(config, local_info, dispatcher, cm, random, scope,
                                             std::move(rest_legacy_constructor), rest_method,
                                             grpc_method);
}
} // namespace Upstream
} // namespace Envoy