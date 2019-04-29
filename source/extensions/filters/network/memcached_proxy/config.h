#pragma once

#include <string>

#include "envoy/config/filter/network/memcached_proxy/v2/memcached_proxy.pb.h"
#include "envoy/config/filter/network/memcached_proxy/v2/memcached_proxy.pb.validate.h"

#include "extensions/filters/network/common/factory_base.h"
#include "extensions/filters/network/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

/**
 * Config registration for the memcached proxy filter. @see NamedNetworkFilterConfigFactory.
 */
class MemcachedProxyFilterConfigFactory
    : public Common::FactoryBase<envoy::config::filter::network::memcached_proxy::v2::MemcachedProxy> {
public:
  MemcachedProxyFilterConfigFactory() : FactoryBase(NetworkFilterNames::get().MemcachedProxy) {}

private:
  Network::FilterFactoryCb createFilterFactoryFromProtoTyped(
      const envoy::config::filter::network::memcached_proxy::v2::MemcachedProxy& proto_config,
      Server::Configuration::FactoryContext& context) override;
};

}
}
}
}
