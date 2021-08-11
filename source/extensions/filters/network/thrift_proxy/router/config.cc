#include "source/extensions/filters/network/thrift_proxy/router/config.h"

#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/router/v3/router.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"
#include "source/extensions/filters/network/thrift_proxy/router/shadow_writer_impl.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ThriftProxy {
namespace Router {

ThriftFilters::FilterFactoryCb RouterFilterConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::network::thrift_proxy::router::v3::Router& proto_config,
    const std::string& stat_prefix, Server::Configuration::FactoryContext& context) {
  UNREFERENCED_PARAMETER(proto_config);

  auto shadow_writer = std::make_shared<ShadowWriterImpl>(context.clusterManager(), stat_prefix,
                                                          context.scope(), context.dispatcher());

  return [&context, stat_prefix,
          shadow_writer](ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Router>(
        context.clusterManager(), stat_prefix, context.scope(), context.runtime(), *shadow_writer));
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
