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
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/oauth2/filter.h"
#include "source/extensions/filters/http/oauth2/oauth.h"

#include "absl/strings/str_cat.h"

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

  // Reject headers that must not be set by configuration (pseudo-headers such as ":path" and the
  // "Host" header) for the forwarded ID token.
  if (proto_config.has_forward_id_token() &&
      !Http::HeaderUtility::isModifiableHeader(proto_config.forward_id_token().header())) {
    return absl::InvalidArgumentError(absl::StrCat(
        "invalid forward_id_token configuration: header '",
        proto_config.forward_id_token().header(),
        "' can not be used to forward the ID token; pseudo-headers and the Host header are not "
        "allowed"));
  }

  // The forward_id_token header is owned by Envoy. Reject configs whose pass_through_matcher keys
  // on it: otherwise a client could control the pass-through decision (and thus bypass OAuth
  // entirely) simply by sending that header.
  if (proto_config.has_forward_id_token()) {
    const Http::LowerCaseString id_token_header(proto_config.forward_id_token().header());
    for (const auto& matcher : proto_config.pass_through_matcher()) {
      if (Http::LowerCaseString(matcher.name()) == id_token_header) {
        return absl::InvalidArgumentError(
            absl::StrCat("invalid forward_id_token configuration: pass_through_matcher can not "
                         "match on the forwarded ID token header '",
                         proto_config.forward_id_token().header(), "'"));
      }
    }
  }

  // The Authorization header has at most one owner. It is written by forward_bearer_token (the
  // access token) and by forward_id_token when its target header is "Authorization" (the ID
  // token); preserve_authorization_header instead keeps the client-supplied value. None of these
  // can be combined, since they would otherwise fight over the same header.
  const bool forward_id_token_on_authorization_header =
      proto_config.has_forward_id_token() &&
      Http::LowerCaseString(proto_config.forward_id_token().header()) ==
          Http::CustomHeaders::get().Authorization;
  if (static_cast<int>(proto_config.forward_bearer_token()) +
          static_cast<int>(forward_id_token_on_authorization_header) +
          static_cast<int>(proto_config.preserve_authorization_header()) >
      1) {
    return absl::InvalidArgumentError(
        "invalid OAuth2 configuration: at most one of forward_bearer_token, "
        "preserve_authorization_header, or forward_id_token (when forwarding the ID token on the "
        "Authorization header) may be set, as they all use the Authorization header");
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
      createFilterConfig(proto.config(), context, std::nullopt, context.scope(), "");
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
