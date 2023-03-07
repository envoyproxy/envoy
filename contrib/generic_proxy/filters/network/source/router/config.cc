#include "contrib/generic_proxy/filters/network/source/router/config.h"

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {
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
} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
