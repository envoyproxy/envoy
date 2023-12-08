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
} // namespace

Http::FilterFactoryCb OAuth2Config::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::oauth2::v3::OAuth2& proto,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  if (!proto.has_config()) {
    throw EnvoyException("config must be present for global config");
  }

  const auto& proto_config = proto.config();
  const auto& credentials = proto_config.credentials();

  const auto& token_secret = credentials.token_secret();
  const auto& hmac_secret = credentials.hmac_secret();

  auto& cluster_manager = context.serverFactoryContext().clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider_token_secret = secretsProvider(
      token_secret, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_token_secret == nullptr) {
    throw EnvoyException("invalid token secret configuration");
  }
  auto secret_provider_hmac_secret =
      secretsProvider(hmac_secret, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_hmac_secret == nullptr) {
    throw EnvoyException("invalid HMAC secret configuration");
  }

  auto secret_reader =
      std::make_shared<SDSSecretReader>(secret_provider_token_secret, secret_provider_hmac_secret,
                                        context.serverFactoryContext().api());
  auto config = std::make_shared<FilterConfig>(proto_config, cluster_manager, secret_reader,
                                               context.scope(), stats_prefix);

  return
      [&context, config, &cluster_manager](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        std::unique_ptr<OAuth2Client> oauth_client =
            std::make_unique<OAuth2ClientImpl>(cluster_manager, config->oauthTokenEndpoint());
        callbacks.addStreamFilter(std::make_shared<OAuth2Filter>(
            config, std::move(oauth_client), context.serverFactoryContext().timeSource()));
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
