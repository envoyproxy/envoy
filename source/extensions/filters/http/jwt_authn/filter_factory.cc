#include "extensions/filters/http/jwt_authn/filter_factory.h"

#include "envoy/config/filter/http/jwt_authn/v2alpha/config.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/config/datasource.h"

#include "extensions/filters/http/jwt_authn/filter.h"

#include "jwt_verify_lib/jwks.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Jwks;
using ::google::jwt_verify::Status;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

/**
 * Validate inline jwks, make sure they are the valid
 */
void validateJwtConfig(const JwtAuthentication& proto_config) {
  for (const auto& rule : proto_config.rules()) {
    const auto inline_jwks = Config::DataSource::read(rule.local_jwks(), true);
    if (!inline_jwks.empty()) {
      auto jwks_obj = Jwks::createFrom(inline_jwks, Jwks::JWKS);
      if (jwks_obj->getStatus() != Status::Ok) {
        throw EnvoyException(
            fmt::format("Issuer '{}' in jwt_authn config has invalid local jwks: {}", rule.issuer(),
                        ::google::jwt_verify::getStatusString(jwks_obj->getStatus())));
      }
    }
  }
}

} // namespace

Http::FilterFactoryCb
FilterFactory::createFilterFactoryFromProtoTyped(const JwtAuthentication& proto_config,
                                                 const std::string& prefix,
                                                 Server::Configuration::FactoryContext& context) {
  validateJwtConfig(proto_config);
  auto filter_config = std::make_shared<FilterConfig>(proto_config, prefix, context);
  return [filter_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    callbacks.addStreamDecoderFilter(
        std::make_shared<Filter>(filter_config->stats(), Authenticator::create(filter_config)));
  };
}

/**
 * Static registration for this jwt_authn filter. @see RegisterFactory.
 */
static Registry::RegisterFactory<FilterFactory, Server::Configuration::NamedHttpFilterConfigFactory>
    register_;

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
