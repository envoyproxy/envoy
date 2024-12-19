#include "source/extensions/filters/http/api_key_auth/config.h"

#include "source/common/config/datasource.h"
#include "source/extensions/filters/http/api_key_auth/api_key_auth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ApiKeyAuth {

absl::StatusOr<Http::FilterFactoryCb> ApiKeyAuthFilterFactory::createFilterFactoryFromProtoTyped(
    const ApiKeyAuthProto& proto_config, const std::string& stats_prefix,
    Server::Configuration::FactoryContext& context) {

  absl::Status status = absl::OkStatus();
  FilterConfigSharedPtr config =
      std::make_shared<FilterConfig>(proto_config, context.scope(), stats_prefix, status);
  RETURN_IF_NOT_OK_REF(status);

  return [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<ApiKeyAuthFilter>(config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
ApiKeyAuthFilterFactory::createRouteSpecificFilterConfigTyped(
    const ApiKeyAuthPerRouteProto& proto_config, Server::Configuration::ServerFactoryContext&,
    ProtobufMessage::ValidationVisitor&) {
  absl::Status status = absl::OkStatus();

  auto config = std::make_shared<RouteConfig>(proto_config, status);
  RETURN_IF_NOT_OK_REF(status);

  return config;
}

REGISTER_FACTORY(ApiKeyAuthFilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace ApiKeyAuth
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
