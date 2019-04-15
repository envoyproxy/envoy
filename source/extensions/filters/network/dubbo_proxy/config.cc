#include "extensions/filters/network/dubbo_proxy/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/dubbo_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace DubboProxy {

Network::FilterFactoryCb DubboProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::network::dubbo_proxy::v2alpha1::DubboProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("dubbo.{}.", proto_config.stat_prefix());

  Filter::ConfigProtocolType protocol_type = proto_config.protocol_type();
  Filter::ConfigSerializationType serialization_type = proto_config.serialization_type();

  return [stat_prefix, protocol_type, serialization_type,
          &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(stat_prefix, protocol_type,
                                                      serialization_type, context.scope(),
                                                      context.dispatcher().timeSource()));
  };
}

/**
 * Static registration for the dubbo filter. @see RegisterFactory.
 */
REGISTER_FACTORY(DubboProxyFilterConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace DubboProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
