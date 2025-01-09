#include "source/extensions/filters/http/header_mutation/config.h"

#include <memory>

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderMutation {

absl::StatusOr<Http::FilterFactoryCb>
HeaderMutationFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig& config, const std::string&, DualInfo,
    Server::Configuration::ServerFactoryContext&) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config = std::make_shared<HeaderMutationConfig>(config, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<HeaderMutation>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
HeaderMutationFactoryConfig::createRouteSpecificFilterConfigTyped(
    const PerRouteProtoConfig& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {
  absl::Status creation_status = absl::OkStatus();
  auto route_config = std::make_shared<PerRouteHeaderMutation>(proto_config, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return route_config;
}

using UpstreamHeaderMutationFactoryConfig = HeaderMutationFactoryConfig;

REGISTER_FACTORY(HeaderMutationFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);
REGISTER_FACTORY(UpstreamHeaderMutationFactoryConfig,
                 Server::Configuration::UpstreamHttpFilterConfigFactory);

} // namespace HeaderMutation
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
