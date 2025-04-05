#include "source/extensions/filters/http/oauth2/config.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/oauth2/v3/oauth.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/filter.h"
#include "source/extensions/filters/http/oauth2/oauth.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Oauth2 {

namespace {
Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Secret::SecretManager& secret_manager,
                Server::Configuration::TransportSocketFactoryContext& transport_socket_factory,
                Init::Manager& init_manager) {
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(config.sds_config(), config.name(),
                                                            transport_socket_factory, init_manager);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}

absl::StatusOr<FilterConfigSharedPtr>
createConfig(const envoy::extensions::filters::http::oauth2::v3::OAuth2& proto,
             Server::Configuration::CommonFactoryContext& context,
             Server::Configuration::TransportSocketFactoryContext& transport_socket_factory,
             Init::Manager& init_manager) {
  if (!proto.has_config()) {
    // Null config will be accepted as a special case where the filter will be disabled.
    return nullptr;
  }

  auto& secret_manager = context.clusterManager().clusterManagerFactory().secretManager();

  const auto& proto_config = proto.config();
  const auto& credentials = proto_config.credentials();

  auto secret_provider_client_secret = secretsProvider(credentials.token_secret(), secret_manager,
                                                       transport_socket_factory, init_manager);
  if (secret_provider_client_secret == nullptr) {
    return absl::InvalidArgumentError("invalid token secret configuration");
  }

  auto secret_provider_hmac_secret = secretsProvider(credentials.hmac_secret(), secret_manager,
                                                     transport_socket_factory, init_manager);
  if (secret_provider_hmac_secret == nullptr) {
    return absl::InvalidArgumentError("invalid HMAC secret configuration");
  }

  if (proto_config.preserve_authorization_header() && proto_config.forward_bearer_token()) {
    return absl::InvalidArgumentError(
        "invalid combination of forward_bearer_token and preserve_authorization_header "
        "configuration. If forward_bearer_token is set to true, then "
        "preserve_authorization_header must be false");
  }

  auto secret_reader = std::make_shared<SDSSecretReader>(std::move(secret_provider_client_secret),
                                                         std::move(secret_provider_hmac_secret),
                                                         context.threadLocal(), context.api());
  return std::make_shared<FilterConfig>(proto_config, context, secret_reader);
}

} // namespace

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
OAuth2Config::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2& proto,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  auto status_or_config = createConfig(proto, context, context.getTransportSocketFactoryContext(),
                                       context.initManager());
  RETURN_IF_NOT_OK_REF(status_or_config.status());
  return std::make_shared<RouteSpecificFilterConfig>(std::move(status_or_config.value()));
}

absl::StatusOr<Http::FilterFactoryCb> OAuth2Config::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2& proto,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {

  auto stats_config = std::make_shared<StatsConfig>(stats_prefix, context.scope());
  auto status_or_config =
      createConfig(proto, context.serverFactoryContext(),
                   context.getTransportSocketFactoryContext(), context.initManager());
  RETURN_IF_NOT_OK_REF(status_or_config.status());

  return [&server = context.serverFactoryContext(), config = std::move(status_or_config.value()),
          stats_config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
    auto client_creator = [&server](const FilterConfig& config) {
      return std::make_unique<OAuth2ClientImpl>(server.clusterManager(),
                                                config.oauthTokenEndpoint(), config.retryPolicy(),
                                                config.defaultExpiresIn());
    };
    auto validator_creator = [&server](const FilterConfig& config) {
      return std::make_unique<OAuth2CookieValidator>(server.timeSource(), config.cookieNames(),
                                                     config.cookieDomain());
    };
    callbacks.addStreamFilter(std::make_shared<OAuth2Filter>(
        stats_config, config, std::move(client_creator), std::move(validator_creator),
        server.timeSource(), server.api().randomGenerator()));
  };
}

/*
 * Static registration for the OAuth2 filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OAuth2Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
