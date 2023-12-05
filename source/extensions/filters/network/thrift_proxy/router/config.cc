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
  auto& server_context = context.serverFactoryContext();

  auto stats =
      std::make_shared<const RouterStats>(stat_prefix, context.scope(), server_context.localInfo());
  auto shadow_writer = std::make_shared<ShadowWriterImpl>(server_context.clusterManager(), *stats,
                                                          server_context.mainThreadDispatcher(),
                                                          server_context.threadLocal());
  bool close_downstream_on_error =
      PROTOBUF_GET_WRAPPED_OR_DEFAULT(proto_config, close_downstream_on_upstream_error, true);

  return [&context, stats, shadow_writer, close_downstream_on_error](
             ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Router>(
        context.serverFactoryContext().clusterManager(), *stats,
        context.serverFactoryContext().runtime(), *shadow_writer, close_downstream_on_error));
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
