#include "source/common/upstream/eds_subscription_factory.h"

#include <assert.h>

#include "source/common/config/type_to_endpoint.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Upstream {

EdsSubscriptionFactory::EdsSubscriptionFactory(
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, Api::Api& api,
    ProtobufMessage::ValidationVisitor& validation_visitor, const Server::Instance& server)
    : local_info_(local_info), dispatcher_(dispatcher), cm_(cm), api_(api),
      validation_visitor_(validation_visitor), server_(server) {}

Config::SubscriptionPtr EdsSubscriptionFactory::subscriptionFromConfigSource(
    const envoy::config::core::v3::ConfigSource& config, absl::string_view type_url,
    Stats::Scope& scope, Config::SubscriptionCallbacks& callbacks,
    Config::OpaqueResourceDecoder& resource_decoder, const Config::SubscriptionOptions& options) {
  if (config.config_source_specifier_case() ==
          envoy::config::core::v3::ConfigSource::kApiConfigSource &&
      config.api_config_source().api_type() == envoy::config::core::v3::ApiConfigSource::GRPC) {
    const envoy::config::core::v3::ApiConfigSource& api_config_source = config.api_config_source();
    Config::CustomConfigValidatorsPtr custom_config_validators =
        std::make_unique<Config::CustomConfigValidatorsImpl>(validation_visitor_, server_,
                                                             api_config_source.config_validators());
    Config::GrpcMuxSharedPtr mux_to_use = getOrCreateMux(
        Config::Utility::factoryForGrpcApiConfigSource(
            cm_.grpcAsyncClientManager(), api_config_source, scope, /*skip_cluster_check*/ true)
            ->createUncachedRawAsyncClient(),
        Config::sotwGrpcMethod(type_url), api_.randomGenerator(), api_config_source, scope,
        Config::Utility::parseRateLimitSettings(api_config_source), custom_config_validators);

    Config::SubscriptionStats stats = Config::Utility::generateStats(scope);
    return std::make_unique<Config::GrpcSubscriptionImpl>(
        mux_to_use, callbacks, resource_decoder, stats, type_url, dispatcher_,
        Config::Utility::configSourceInitialFetchTimeout(config), /*is_aggregated*/ false, options);
  }
  return cm_.subscriptionFactory().subscriptionFromConfigSource(config, type_url, scope, callbacks,
                                                                resource_decoder, options);
}

Config::SubscriptionPtr EdsSubscriptionFactory::collectionSubscriptionFromUrl(
    const xds::core::v3::ResourceLocator&, const envoy::config::core::v3::ConfigSource&,
    absl::string_view, Stats::Scope&, Config::SubscriptionCallbacks&,
    Config::OpaqueResourceDecoder&) {
  PANIC("not implemented");
}

Config::GrpcMuxSharedPtr EdsSubscriptionFactory::getOrCreateMux(
    Grpc::RawAsyncClientPtr async_client, const Protobuf::MethodDescriptor& service_method,
    Random::RandomGenerator& random, const envoy::config::core::v3::ApiConfigSource& config_source,
    Stats::Scope& scope, const Config::RateLimitSettings& rate_limit_settings,
    Config::CustomConfigValidatorsPtr& custom_config_validators) {
  const uint64_t mux_key = MessageUtil::hash(config_source.grpc_services(0));
  if (muxes_.find(mux_key) == muxes_.end()) {
    muxes_.emplace(std::make_pair(mux_key, std::make_shared<Config::GrpcMuxImpl>(
                                               local_info_, std::move(async_client), dispatcher_,
                                               service_method, random, scope, rate_limit_settings,
                                               config_source.set_node_on_first_message_only(),
                                               std::move(custom_config_validators))));
  }
  return muxes_.at(mux_key);
}

} // namespace Upstream
} // namespace Envoy
