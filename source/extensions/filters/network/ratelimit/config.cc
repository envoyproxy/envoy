#include "extensions/filters/network/ratelimit/config.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/network/ratelimit/ratelimit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RateLimitFilter {

Server::Configuration::NetworkFilterFactoryCb RateLimitConfigFactory::createFilter(
    const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.domain().empty());
  ASSERT(proto_config.descriptors_size() > 0);

  ConfigSharedPtr filter_config(new Config(proto_config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);

  return [filter_config, timeout_ms, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(
        filter_config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms))));
  };
}

Server::Configuration::NetworkFilterFactoryCb
RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                            Server::Configuration::FactoryContext& context) {
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config;
  Envoy::Config::FilterJson::translateTcpRateLimitFilter(json_config, proto_config);
  return createFilter(proto_config, context);
}

Server::Configuration::NetworkFilterFactoryCb RateLimitConfigFactory::createFilterFactoryFromProto(
    const Protobuf::Message& proto_config, Server::Configuration::FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::rate_limit::v2::RateLimit&>(proto_config),
      context);
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
