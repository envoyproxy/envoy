#include "source/extensions/filters/http/jwt_authn/filter_factory.h"

#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"
#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "source/common/config/datasource.h"
#include "source/common/jwt/jwks.h"
#include "source/extensions/filters/http/jwt_authn/filter.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

using JwtVerify::Jwks;
using JwtVerify::Status;

/**
 * Validate inline jwks, make sure they are the valid
 */
void validateJwtConfig(const JwtAuthentication& proto_config, Api::Api& api) {
  for (const auto& [name, provider] : proto_config.providers()) {
    const auto inline_jwks = THROW_OR_RETURN_VALUE(
        Config::DataSource::read(provider.local_jwks(), true, api), std::string);
    if (!inline_jwks.empty()) {
      auto jwks_obj = Jwks::createFrom(inline_jwks, Jwks::JWKS);
      if (jwks_obj->getStatus() != Status::Ok) {
        throw EnvoyException(
            fmt::format("Provider '{}' in jwt_authn config has invalid local jwks: {}", name,
                        JwtVerify::getStatusString(jwks_obj->getStatus())));
      }
    }
  }
}

} // namespace

Http::FilterFactoryCb
FilterFactory::createFilterFactoryFromProtoTyped(const JwtAuthentication& proto_config,
                                                 const std::string& prefix,
                                                 Server::Configuration::FactoryContext& context) {
  validateJwtConfig(proto_config, context.serverFactoryContext().api());
  auto filter_config = std::make_shared<FilterConfigImpl>(proto_config, prefix, context);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(std::make_shared<Filter>(filter_config));
  };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
FilterFactory::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::jwt_authn::v3::PerRouteConfig& per_route,
    Envoy::Server::Configuration::ServerFactoryContext&,
    Envoy::ProtobufMessage::ValidationVisitor&) {
  return std::make_shared<PerRouteFilterConfig>(per_route);
}

/**
 * Static registration for this jwt_authn filter. @see RegisterFactory.
 */
REGISTER_FACTORY(FilterFactory, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
