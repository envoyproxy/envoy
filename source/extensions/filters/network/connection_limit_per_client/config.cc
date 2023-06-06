#include "source/extensions/filters/network/connection_limit_per_client/config.h"

#include "envoy/extensions/filters/network/connection_limit_per_client/v3/connection_limit_per_client.pb.h"
#include "envoy/extensions/filters/network/connection_limit_per_client/v3/connection_limit_per_client.pb.validate.h"

#include "source/extensions/filters/network/connection_limit_per_client/connection_limit_per_client.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ConnectionLimitPerClientFilter {

Network::FilterFactoryCb ConnectionLimitPerClientConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::connection_limit_per_client::v3::ConnectionLimitPerClient& proto_config,
    Server::Configuration::FactoryContext& context) {
  ConfigSharedPtr filter_config(new Config(proto_config, context.scope(), context.runtime()));
  return [filter_config](Network::FilterManager& filter_manager) -> void {
    filter_manager.addReadFilter(std::make_shared<Filter>(filter_config));
  };
}

/**
 * Static registration for the connection limit per client filter. @see RegisterFactory.
 */
REGISTER_FACTORY(ConnectionLimitPerClientConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace ConnectionLimitPerClientFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
