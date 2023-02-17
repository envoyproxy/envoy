#include "source/extensions/filters/http/header_mutation/config.h"

#include <memory>

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

Http::FilterFactoryCb HeaderMutationFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig&, const std::string&, DualInfo,
    Server::Configuration::ServerFactoryContext&) {
  return [](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<HeaderMutation>());
  };
}

Router::RouteSpecificFilterConfigConstSharedPtr
HeaderMutationFactoryConfig::createRouteSpecificFilterConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<PerRouteHeaderMutation>(proto_config);
}

using UpstreamHeaderMutationFactoryConfig = HeaderMutationFactoryConfig;

REGISTER_FACTORY(HeaderMutationFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamHeaderMutationFactoryConfig,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
