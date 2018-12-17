#include "extensions/filters/network/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/common/ratelimit/ratelimit_impl.h"
#include "extensions/filters/common/ratelimit/ratelimit_registration.h"
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
  const std::chrono::milliseconds timeout =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20));
  Filters::Common::RateLimit::ClientFactoryPtr client_factory =
      Filters::Common::RateLimit::rateLimitClientFactory(context);
  // If ratelimit service config is provided in both bootstrap and filter, we should validate that
  // they are same.
  Filters::Common::RateLimit::validateRateLimitConfig<
      const envoy::config::filter::network::rate_limit::v2::RateLimit&>(proto_config,
                                                                        client_factory);

  return [client_factory, proto_config, &context, timeout,
          filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(
        filter_config,

        Filters::Common::RateLimit::rateLimitClient(
            client_factory, context, proto_config.rate_limit_service().grpc_service(), timeout)));
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
