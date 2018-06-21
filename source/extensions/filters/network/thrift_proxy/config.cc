#include "extensions/filters/network/thrift_proxy/config.h"

#include <string>

#include "envoy/network/connection.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/thrift_proxy/filter.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {

Network::FilterFactoryCb ThriftProxyFilterConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::v2alpha1::ThriftProxy& proto_config,
    Server::Configuration::FactoryContext& context) {
  ASSERT(!proto_config.stat_prefix().empty());

  const std::string stat_prefix = fmt::format("thrift.{}.", proto_config.stat_prefix());

  return [stat_prefix, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<Filter>(stat_prefix, context.scope()));
  };
}

/**
 * Static registration for the thrift filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<ThriftProxyFilterConfigFactory,
                                 Server::Configuration::NamedNetworkFilterConfigFactory>
    registered_;

} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
