#include "extensions/filters/network/thrift_proxy/router/config.h"

#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.h"
#include "envoy/config/filter/thrift/router/v2alpha1/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "extensions/filters/network/thrift_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

ThriftFilters::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::config::filter::thrift::router::v2alpha1::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);

  return [&context, stat_prefix](ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(
        std::make_shared<Router>(context.clusterManager(), stat_prefix, context.scope()));
  };
}

/**
 * Static registration for the router filter. @see RegisterFactory.
 */
REGISTER_FACTORY(RouterFilterConfig, ThriftFilters::NamedThriftFilterConfigFactory);

} // namespace Router
} // namespace ThriftProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
