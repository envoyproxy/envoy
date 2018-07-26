#include "extensions/filters/network/thrift_proxy/router/config.h"

#include "envoy/registry/registry.h"

#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

ThriftFilters::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::v2alpha1::router::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);
  UNREFERENCED_PARAMETER(stat_prefix);

  return [&context](ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Router>(context.clusterManager()));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<RouterFilterConfig, ThriftFilters::NamedThriftFilterConfigFactory>
    register_;

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
