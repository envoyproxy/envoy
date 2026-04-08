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
#include "source/common/common/logger.h"
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
                Server::Configuration::ServerFactoryContext& server_context,
                OptRef<Init::Manager> init_manager) {
  if (config.has_sds_config()) {
    return server_context.secretManager().findOrCreateGenericSecretProvider(
        config.sds_config(), config.name(), server_context, init_manager);
  } else {
    return server_context.secretManager().findStaticGenericSecretProvider(config.name());
  }
}

absl::StatusOr<FilterConfigSharedPtr>
createFilterConfig(const envoy::extensions::filters::http::oauth2::v3::OAuth2Config& proto_config,
                   Server::Configuration::ServerFactoryContext& server_context,
                   OptRef<Init::Manager> init_manager, Stats::Scope& scope,
                   const std::string& stats_prefix) {
  const auto& credentials = proto_config.credentials();
  const auto auth_type = proto_config.auth_type();

  // token_secret is required unless auth_type is TLS_CLIENT_AUTH
  if (auth_type !=
      envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType_TLS_CLIENT_AUTH) {
    if (!credentials.has_token_secret()) {
      return absl::InvalidArgumentError(
          "token_secret is required when auth_type is not TLS_CLIENT_AUTH");
    }
  }
  if (auth_type ==
          envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType_TLS_CLIENT_AUTH &&
      credentials.has_token_secret()) {
    ENVOY_LOG_MISC(debug,
                   "OAuth2 filter: token_secret is ignored when auth_type is TLS_CLIENT_AUTH");
  }

  Secret::GenericSecretConfigProviderSharedPtr secret_provider_client_secret = nullptr;
  if (credentials.has_token_secret() &&
      auth_type !=
          envoy::extensions::filters::http::oauth2::v3::OAuth2Config_AuthType_TLS_CLIENT_AUTH) {
    secret_provider_client_secret =
        secretsProvider(credentials.token_secret(), server_context, init_manager);
    if (secret_provider_client_secret == nullptr) {
      return absl::InvalidArgumentError("invalid token secret configuration");
    }
  }

  auto secret_provider_hmac_secret =
      secretsProvider(credentials.hmac_secret(), server_context, init_manager);
  if (secret_provider_hmac_secret == nullptr) {
    return absl::InvalidArgumentError("invalid HMAC secret configuration");
  }

  if (proto_config.preserve_authorization_header() && proto_config.forward_bearer_token()) {
    return absl::InvalidArgumentError(
        "invalid combination of forward_bearer_token and preserve_authorization_header "
        "configuration. If forward_bearer_token is set to true, then "
        "preserve_authorization_header must be false");
  }

  auto secret_reader = std::make_shared<SDSSecretReader>(
      std::move(secret_provider_client_secret), std::move(secret_provider_hmac_secret),
      server_context.threadLocal(), server_context.api());
  return std::make_shared<FilterConfig>(proto_config, server_context, secret_reader, scope,
                                        stats_prefix);
}
} // namespace

absl::StatusOr<Http::FilterFactoryCb> OAuth2Config::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2& proto,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto& server_context = context.serverFactoryContext();
  auto& cluster_manager = context.serverFactoryContext().clusterManager();
  FilterConfigSharedPtr config = nullptr;
  if (proto.has_config()) {
    auto config_or_error = createFilterConfig(proto.config(), server_context, context.initManager(),
                                              context.scope(), stats_prefix);
    if (!config_or_error.ok()) {
      return config_or_error.status();
    }
    config = config_or_error.value();
  }

  return
      [&context, config, &cluster_manager](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamFilter(std::make_shared<OAuth2Filter>(
            config,
            [&cluster_manager](const FilterConfig& active_config) -> std::shared_ptr<OAuth2Client> {
              return std::make_shared<OAuth2ClientImpl>(
                  cluster_manager, active_config.oauthTokenEndpoint(), active_config.retryPolicy(),
                  active_config.defaultExpiresIn());
            },
            [](TimeSource& time_source,
               const FilterConfig& active_config) -> std::shared_ptr<CookieValidator> {
              return std::make_shared<OAuth2CookieValidator>(
                  time_source, active_config.cookieNames(), active_config.cookieDomain());
            },
            context.serverFactoryContext().timeSource(),
            context.serverFactoryContext().api().randomGenerator()));
      };
}

absl::StatusOr<Router::RouteSpecificFilterConfigConstSharedPtr>
OAuth2Config::createRouteSpecificFilterConfigTyped(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2PerRoute& proto,
    Server::Configuration::ServerFactoryContext& context, ProtobufMessage::ValidationVisitor&) {
  auto config_or_error =
      createFilterConfig(proto.config(), context, absl::nullopt, context.scope(), "");
  if (!config_or_error.ok()) {
    return config_or_error.status();
  }
  return config_or_error.value();
}

/*
 * Static registration for the OAuth2 filter. @see RegisterFactory.
 */
REGISTER_FACTORY(OAuth2Config, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Oauth2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
