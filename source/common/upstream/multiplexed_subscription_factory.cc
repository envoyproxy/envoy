#include "source/common/upstream/multiplexed_subscription_factory.h"

#include "source/common/common/hash.h"
#include "source/common/config/utility.h"

namespace Envoy {
namespace Upstream {

MultiplexedSubscriptionFactory::MultiplexedSubscriptionFactory(
    const LocalInfo::LocalInfo& local_info, Event::Dispatcher& dispatcher,
    Upstream::ClusterManager& cm, ProtobufMessage::ValidationVisitor& validation_visitor,
    Api::Api& api, const Server::Instance& server,
    Config::XdsResourcesDelegateOptRef xds_resources_delegate,
    Config::XdsConfigTrackerOptRef xds_config_tracker)
    : Config::SubscriptionFactoryImpl(local_info, dispatcher, cm, validation_visitor, api, server,
                                      xds_resources_delegate, xds_config_tracker){};

Config::GrpcMuxSharedPtr MultiplexedSubscriptionFactory::getOrCreateMux(
    const envoy::config::core::v3::ApiConfigSource& config_source, absl::string_view type_url,
    Stats::Scope& scope, Config::CustomConfigValidatorsPtr& custom_config_validators) {
  if (config_source.api_type() == envoy::config::core::v3::ApiConfigSource::GRPC ||
      config_source.api_type() == envoy::config::core::v3::ApiConfigSource::DELTA_GRPC) {
    const uint64_t xds_server_hash = MessageUtil::hash(config_source.grpc_services(0));
    const uint64_t xds_type_hash = HashUtil::xxHash64(type_url);
    const uint64_t mux_key = xds_server_hash ^ xds_type_hash;
    if (muxes_.find(mux_key) == muxes_.end()) {
      muxes_.emplace(
          std::make_pair(mux_key, Config::SubscriptionFactoryImpl::getOrCreateMux(
                                      config_source, type_url, scope, custom_config_validators)));
    }
    return muxes_.at(mux_key);
  } else {
    return Config::SubscriptionFactoryImpl::getOrCreateMux(config_source, type_url, scope,
                                                           custom_config_validators);
  }
}

} // namespace Upstream
} // namespace Envoy
