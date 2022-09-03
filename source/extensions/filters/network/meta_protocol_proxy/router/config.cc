#include "source/extensions/filters/network/meta_protocol_proxy/router/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace MetaProtocolProxy {
namespace Router {

FilterFactoryCb
RouterFactory::createFilterFactoryFromProto(const Protobuf::Message&, const std::string&,
                                            Server::Configuration::FactoryContext& context) {
  return [&context](FilterChainFactoryCallbacks& callbacks) {
    callbacks.addDecoderFilter(std::make_shared<RouterFilter>(context));
  };
}

REGISTER_FACTORY(RouterFactory, NamedFilterConfigFactory);

} // namespace Router
} // namespace MetaProtocolProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
