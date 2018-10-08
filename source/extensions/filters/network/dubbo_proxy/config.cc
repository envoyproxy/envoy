#include "extensions/filters/network/dubbo_proxy/config.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
    Server::Configuration::FactoryContext&) {
  const std::string stat_prefix = fmt::format("dubbo.{}.", proto_config.stat_prefix());

  return [](Network::FilterManager&) -> void {};
}

/**
 * Static registration for the dubbo filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<DubboProxyFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy