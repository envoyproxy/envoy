#include "source/extensions/filters/network/generic_proxy/router/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
namespace Router {

FilterFactoryCb
RouterFactory::createFilterFactoryFromProto(const Protobuf::Message& config, const std::string&,
                                            Server::Configuration::FactoryContext& context) {
  const auto& typed_config = MessageUtil::downcastAndValidate<
      const envoy::extensions::filters::network::generic_proxy::router::v3::Router&>(
      config, context.messageValidationVisitor());

  auto router_config = std::make_shared<RouterConfig>(typed_config);

  return [&context, router_config](FilterChainFactoryCallbacks& callbacks) {
    callbacks.addDecoderFilter(std::make_shared<RouterFilter>(router_config, context));
  };
}

REGISTER_FACTORY(RouterFactory, NamedFilterConfigFactory);

} // namespace Router
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
