#include "source/extensions/filters/http/transform/config.h"

#include <memory>

#include "envoy/registry/registry.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Transform {

absl::StatusOr<Http::FilterFactoryCb> TransformFactoryConfig::createFilterFactoryFromProtoTyped(
    const ProtoConfig& proto_config, const std::string& stat_prefix,
    Server::Configuration::FactoryContext& context) {
  absl::Status creation_status = absl::OkStatus();
  auto filter_config =
      std::make_shared<FilterConfig>(proto_config, stat_prefix, context, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);

  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamFilter(std::make_shared<TransformFilter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
TransformFactoryConfig::createRouteSpecificFilterConfigTyped(
    const ProtoConfig& proto_config, Server::Configuration::ServerFactoryContext& context,
    ProtobufMessage::ValidationVisitor&) {
  absl::Status creation_status = absl::OkStatus();
  auto route_config = std::make_shared<TransformConfig>(proto_config, context, creation_status);
  RETURN_IF_NOT_OK_REF(creation_status);
  return route_config;
}

REGISTER_FACTORY(TransformFactoryConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Transform
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
