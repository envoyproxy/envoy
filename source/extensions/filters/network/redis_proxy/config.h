#pragma once

#include <string>

#include "envoy/config/filter/network/redis_proxy/v2/redis_proxy.pb.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * Config registration for the redis proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class RedisProxyFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::redis_proxy::v2::RedisProxy> {
public:
  RedisProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().REDIS_PROXY) {}

  // NamedNetworkFilterConfigFactory
  Network::FilterFactoryCb
  createFilterFactory(const Json::Object& json_config,
                      Server::Configuration::FactoryContext& context) override;

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::redis_proxy::v2::RedisProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
