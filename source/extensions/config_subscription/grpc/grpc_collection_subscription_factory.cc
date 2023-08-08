#include "source/extensions/config_subscription/grpc/grpc_collection_subscription_factory.h"

#include "source/common/config/custom_config_validators_impl.h"
#include "source/common/config/type_to_endpoint.h"
#include "source/extensions/config_subscription/grpc/grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/grpc_subscription_impl.h"
#include "source/extensions/config_subscription/grpc/new_grpc_mux_impl.h"
#include "source/extensions/config_subscription/grpc/xds_mux/grpc_mux_impl.h"

namespace Envoy {
namespace Config {

SubscriptionPtr DeltaGrpcCollectionConfigSubscriptionFactory::create(
    ConfigSubscriptionFactory::SubscriptionData& data) {
  const envoy::config::core::v3::ApiConfigSource& api_config_source =
      data.config_.api_config_source();
  CustomConfigValidatorsPtr custom_config_validators = std::make_unique<CustomConfigValidatorsImpl>(
      data.validation_visitor_, data.server_, api_config_source.config_validators());

  JitteredExponentialBackOffStrategyPtr backoff_strategy =
      Utility::prepareJitteredExponentialBackOffStrategy(
          api_config_source, data.api_.randomGenerator(), SubscriptionFactory::RetryInitialDelayMs,
          SubscriptionFactory::RetryMaxDelayMs);

  GrpcMuxContext grpc_mux_context{
      /*async_client_=*/Config::Utility::factoryForGrpcApiConfigSource(
          data.cm_.grpcAsyncClientManager(), api_config_source, data.scope_, true)
          ->createUncachedRawAsyncClient(),
      /*dispatcher_=*/data.dispatcher_,
      /*service_method_=*/deltaGrpcMethod(data.type_url_),
      /*local_info_=*/data.local_info_,
      /*rate_limit_settings_=*/Utility::parseRateLimitSettings(api_config_source),
      /*scope_=*/data.scope_,
      /*config_validators_=*/std::move(custom_config_validators),
      /*xds_resources_delegate_=*/{},
      /*xds_config_tracker_=*/data.xds_config_tracker_,
      /*backoff_strategy_=*/std::move(backoff_strategy),
      /*target_xds_authority_=*/"",
      /*eds_resources_cache_=*/nullptr // No EDS resources cache needed from collections.
  };
  return std::make_unique<GrpcCollectionSubscriptionImpl>(
      data.collection_locator_.value(), std::make_shared<Config::NewGrpcMuxImpl>(grpc_mux_context),
      data.callbacks_, data.resource_decoder_, data.stats_, data.dispatcher_,
      Utility::configSourceInitialFetchTimeout(data.config_), /*is_aggregated=*/false,
      data.options_);
}

SubscriptionPtr AggregatedGrpcCollectionConfigSubscriptionFactory::create(
    ConfigSubscriptionFactory::SubscriptionData& data) {
  return std::make_unique<GrpcCollectionSubscriptionImpl>(
      data.collection_locator_.value(), data.cm_.adsMux(), data.callbacks_, data.resource_decoder_,
      data.stats_, data.dispatcher_, Utility::configSourceInitialFetchTimeout(data.config_),
      /*is_aggregated=*/true, data.options_);
}

SubscriptionPtr
AdsCollectionConfigSubscriptionFactory::create(ConfigSubscriptionFactory::SubscriptionData& data) {
  // All Envoy collections currently are xDS resource graph roots and require node context
  // parameters.
  return std::make_unique<GrpcCollectionSubscriptionImpl>(
      data.collection_locator_.value(), data.cm_.adsMux(), data.callbacks_, data.resource_decoder_,
      data.stats_, data.dispatcher_, Utility::configSourceInitialFetchTimeout(data.config_), true,
      data.options_);
}

REGISTER_FACTORY(DeltaGrpcCollectionConfigSubscriptionFactory, ConfigSubscriptionFactory);
REGISTER_FACTORY(AggregatedGrpcCollectionConfigSubscriptionFactory, ConfigSubscriptionFactory);
REGISTER_FACTORY(AdsCollectionConfigSubscriptionFactory, ConfigSubscriptionFactory);

} // namespace Config
} // namespace Envoy
