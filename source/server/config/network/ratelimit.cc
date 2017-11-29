#include "server/config/network/ratelimit.h"

#include <chrono>
#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/config/filter_json.h"
#include "common/filter/ratelimit.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Server {
namespace Configuration {

NetworkFilterFactoryCb
RateLimitConfigFactory::createFilter(const envoy::api::v2::filter::network::RateLimit& config,
                                     FactoryContext& context) {

  ASSERT(!config.stat_prefix().empty());
  ASSERT(!config.domain().empty());
  ASSERT(config.has_descriptors());

  RateLimit::TcpFilter::ConfigSharedPtr config(
      new RateLimit::TcpFilter::Config(config, context.scope(), context.runtime()));
  const uint32_t timeout_ms = PROTOBUF_GET_MS_OR_DEFAULT(config, timeout, 20);

  return [config, timeout_ms, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(Network::ReadFilterSharedPtr{new RateLimit::TcpFilter::Instance(
        config, context.rateLimitClient(std::chrono::milliseconds(timeout_ms)))});
  };
}

NetworkFilterFactoryCb RateLimitConfigFactory::createFilterFactory(const Json::Object& json_config,
                                                                   FactoryContext& context) {
  envoy::api::v2::filter::network::RateLimit config;
  Config::FilterJson::translateTcpRateLimitFilter(json_config, config);
  return createFilter(config, stats_prefix, context);
}

NetworkFilterFactoryCb
RateLimitConfigFactory::createFilterFactoryFromProto(const Protobuf::Message& config,
                                                     FactoryContext& context) {
  return createFilter(dynamic_cast<const envoy::api::v2::filter::network::RateLimit&>(config),
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
