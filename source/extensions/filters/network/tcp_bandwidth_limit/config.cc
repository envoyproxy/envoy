#include "source/extensions/filters/network/tcp_bandwidth_limit/config.h"

#include "envoy/extensions/filters/network/tcp_bandwidth_limit/v3/tcp_bandwidth_limit.pb.h"
#include "envoy/extensions/filters/network/tcp_bandwidth_limit/v3/tcp_bandwidth_limit.pb.validate.h"

#include "source/extensions/filters/network/tcp_bandwidth_limit/tcp_bandwidth_limit.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {

Network::FilterFactoryCb TcpBandwidthLimitConfigFactory::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit&
        proto_config,
    Server::Configuration::FactoryContext& context) {

  auto config = std::make_shared<FilterConfig>(proto_config, context.scope(),
                                               context.serverFactoryContext().runtime(),
                                               context.serverFactoryContext().timeSource());

  return [config, &context](Network::FilterManager& filter_manager) -> void {
    filter_manager.addFilter(std::make_shared<TcpBandwidthLimitFilter>(
        config, context.serverFactoryContext().mainThreadDispatcher()));
  };
}

REGISTER_FACTORY(TcpBandwidthLimitConfigFactory,
                 Server::Configuration::NamedNetworkFilterConfigFactory);

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
