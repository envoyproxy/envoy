#include "source/extensions/filters/network/connection_limit/config.h"

#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.h"
#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.validate.h"

#include "source/extensions/filters/network/connection_limit/connection_limit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitFilter {

Network::FilterFactoryCb ConnectionLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(
      new Config(proto_config, context.scope(), context.serverFactoryContext().runtime()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the connection limit filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConnectionLimitConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ConnectionLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
