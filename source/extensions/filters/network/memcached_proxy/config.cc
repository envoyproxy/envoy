#include "extensions/filters/network/memcached_proxy/config.h"

#include "envoy/config/filter/network/memcached_proxy/v2/memcached_proxy.pb.validate.h"
#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "common/common/fmt.h"

#include "extensions/filters/network/memcached_proxy/proxy.h"
#include "extensions/filters/network/memcached_proxy/codec_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MemcachedProxy {

Network::FilterFactoryCb MemcachedProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::memcached_proxy::v2::MemcachedProxy& proto_config,
    Server::Configuration::FactoryContext& context) {

  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("memcached.{}.", proto_config.stat_prefix());

  return [stat_prefix, &context](Network::FilterManager& filter_manager) -> void {
    DecoderFactoryImpl factory;

    filter_manager.addFilter(std::make_shared<ProxyFilter>(
        stat_prefix, context.scope(),
        // context.runtime(),
        // context.drainDecision(), context.random(),
        // context.dispatcher().timeSource(),
         factory, std::make_unique<EncoderImpl>()));
  };
}

/**
 * Static registration for the memcached filter. @see RegisterFactory.
 */
REGISTER_FACTORY(MemcachedProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

}
}
}
}
