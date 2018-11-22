#include "extensions/filters/network/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/network/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

Network::FilterFactoryCb RateLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.domain().empty());
  ASSERT(proto_config.descriptors_size() > 0);

  ConfigSharedPtr filter_config(new Config(proto_config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);
  // TODO(ramaraochavali): Figure out how to get the registered name here and also see if this code
  // can be used across filters.
  ratelimit_service_config_ =
      context.singletonManager().getTyped<Envoy::RateLimit::RateLimitServiceConfig>(
          "ratelimit_service_config_singleton_name", [] { return nullptr; });
  if (ratelimit_service_config_) {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::GrpcFactoryImpl>(
        ratelimit_service_config_->config_, context.clusterManager().grpcAsyncClientManager(),
        context.scope());
  } else {
    ratelimit_client_factory_ = std::make_unique<Filters::Common::RateLimit::NullFactoryImpl>();
  }
  return
      [filter_config, timeout_ms, &context, this](Network::FilterManager& filter_manager) -> void {
        filter_manager.addReadFilter(std::make_shared<Filter>(
            filter_config,
            ratelimit_client_factory_->create(std::chrono::milliseconds(timeout_ms), context)));
      };
}

Network::FilterFactoryCb
RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                            Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config;
  Envoy::Config::FilterJson::translateTcpRateLimitFilter(json_config, proto_config);
  return createFilterFactoryFromProtoTyped(proto_config, context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace RateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
