#include "server/config/network/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/config/filter/network/rate_limit/v2/rate_limit.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/filter/ratelimit.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb RateLimitConfigFactory::createFilter(
    const envoy::config::filter::network::rate_limit::v2::RateLimit& proto_config,
    FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());
  ASSERT(!proto_config.domain().empty());
  ASSERT(proto_config.descriptors_size() > 0);

  RateLimit::TcpFilter::ConfigSharedPtr filter_config(
      new RateLimit::TcpFilter::Config(proto_config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(proto_config, timeout, 20);

  return [filter_config, timeout_ms, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new RateLimit::TcpFilter::Instance(
        filter_config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms)))});
  };
}

NetworkFilterFactoryCb RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                   FactoryContext& context) {
  envoy::config::filter::network::rate_limit::v2::RateLimit proto_config;
  Config::FilterJson::translateTcpRateLimitFilter(json_config, proto_config);
  return createFilter(proto_config, context);
}

NetworkFilterFactoryCb
RateLimitConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& proto_config,
                                                     FactoryContext& context) {
  return createFilter(
      MessageUtil::downcastAndValidate<
          const envoy::config::filter::network::rate_limit::v2::RateLimit&>(proto_config),
      context);
}

/**
 * Static registration for the rate limit filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RateLimitConfigFactory, NamedNetworkFilterConfigFactory>
    registered_;

} // namespace Configuration
} // namespace Server
} // namespace Envoy
