#include "extensions/filters/network/dubbo_proxy/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/dubbo_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("dubbo.{}.", proto_config.stat_prefix());

  return [stat_prefix, &proto_config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(
        stat_prefix, proto_config.protocol_type(), proto_config.serialization_type(),
        context.scope(), context.dispatcher().timeSystem()));
  };
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
