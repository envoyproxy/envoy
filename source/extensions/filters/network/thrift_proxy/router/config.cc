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

  auto stats =
      std::make_shared<const RouterStats>(stat_prefix, context.scope(), context.localInfo());
  auto shadow_writer = std::make_shared<ShadowWriterImpl>(
      context.clusterManager(), *stats, context.mainThreadDispatcher(), context.threadLocal());
  bool keep_downstream = proto_config.keep_downstream();

  return [&context, stats, shadow_writer,
          keep_downstream](ThriftFilters::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addDecoderFilter(std::make_shared<Router>(
        context.clusterManager(), *stats, context.runtime(), *shadow_writer, keep_downstream));
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
