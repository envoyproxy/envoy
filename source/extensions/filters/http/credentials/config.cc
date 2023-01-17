#include "source/extensions/filters/http/credentials/config.h"

#include <chrono>
#include <memory>
#include <string>

#include "envoy/common/exception.h"
#include "envoy/extensions/filters/http/credentials/v3alpha/injector.pb.validate.h"
#include "envoy/registry/registry.h"
#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/upstream/cluster_manager.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/credentials/filter.h"
#include "source/extensions/filters/http/credentials/source.h"
#include "source/extensions/filters/http/credentials/source_basic_auth.h"
#include "source/extensions/filters/http/credentials/source_bearer_token.h"
#include "source/extensions/filters/http/credentials/source_oauth2_client_credentials_grant.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Credentials {

namespace {
Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Secret::SecretManager& secret_manager,
                Server::Configuration::TransportSocketFactoryContext& transport_socket_factory) {
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(config.sds_config(), config.name(),
                                                            transport_socket_factory);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}
} // namespace

Http::FilterFactoryCb InjectorConfig::createFilterFactoryFromProtoTyped(
    const envoy::extensions::filters::http::credentials::v3alpha::Injector& proto_config,
    const std::string& stats_prefix, Server::Configuration::FactoryContext& context) {
  auto credential_source = createCredentialSource(proto_config.credential(), context);

  auto config = std::make_shared<FilterConfig>(proto_config, std::move(credential_source),
                                            context.scope(), stats_prefix);
  return
      [config](Http::FilterChainFactoryCallbacks& callbacks) -> void {
        callbacks.addStreamDecoderFilter(std::make_shared<Filter>(config));
      };
}

CredentialSourcePtr InjectorConfig::createCredentialSource(
    const envoy::extensions::filters::http::credentials::v3alpha::Credential& proto_config,
    Server::Configuration::FactoryContext& context) {
  switch (proto_config.credential_type_case()) {
  case envoy::extensions::filters::http::credentials::v3alpha::Credential::CredentialTypeCase::kBasic:
    return createBasicAuthCredentialSource(proto_config.basic(), context);
  case envoy::extensions::filters::http::credentials::v3alpha::Credential::CredentialTypeCase::kBearer:
    return createBearerTokenCredentialSource(proto_config.bearer(), context);
  case envoy::extensions::filters::http::credentials::v3alpha::Credential::CredentialTypeCase::kOauth2: {
    switch (proto_config.oauth2().flow_type_case()) {
      case envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential::FlowTypeCase::kClientCredentials:
        return createOauth2ClientCredentialsGrantCredentialSource(proto_config.oauth2(), context);
      case envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential::FlowTypeCase::FLOW_TYPE_NOT_SET:
        throw EnvoyException("oauth2 flow type not set");
    }
    break;
  }
  case envoy::extensions::filters::http::credentials::v3alpha::Credential::CredentialTypeCase::CREDENTIAL_TYPE_NOT_SET:
    throw EnvoyException("credential type not set");
  }
}

CredentialSourcePtr InjectorConfig::createBasicAuthCredentialSource(
  const envoy::extensions::filters::http::credentials::v3alpha::BasicAuthCredential& proto_config,
  Server::Configuration::FactoryContext& context) {
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();

  const auto& username_secret = proto_config.username().secret();
  const auto& password_secret = proto_config.password().secret();

  auto secret_provider_username_secret =
    secretsProvider(username_secret, secret_manager, transport_socket_factory);
  if (secret_provider_username_secret == nullptr) {
    throw EnvoyException("invalid basic auth username secret configuration");
  }

  auto secret_provider_password_secret =
    secretsProvider(password_secret, secret_manager, transport_socket_factory);
  if (secret_provider_password_secret == nullptr) {
    throw EnvoyException("invalid basic auth password secret configuration");
  }

  return std::make_unique<BasicAuthCredentialSource>(context.threadLocal(), context.api(), secret_provider_username_secret, secret_provider_password_secret);
}

CredentialSourcePtr InjectorConfig::createBearerTokenCredentialSource(
  const envoy::extensions::filters::http::credentials::v3alpha::BearerTokenCredential& proto_config,
  Server::Configuration::FactoryContext& context) {
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();

  const auto& token_secret = proto_config.token().secret();

  auto secret_provider_token_secret =
    secretsProvider(token_secret, secret_manager, transport_socket_factory);
  if (secret_provider_token_secret == nullptr) {
    throw EnvoyException("invalid bearer token auth token secret configuration");
  }

  return std::make_unique<BearerTokenCredentialSource>(context.threadLocal(), context.api(), secret_provider_token_secret);  
}

CredentialSourcePtr InjectorConfig::createOauth2ClientCredentialsGrantCredentialSource(
  const envoy::extensions::filters::http::credentials::v3alpha::OAuth2Credential& proto_config,
  Server::Configuration::FactoryContext& context) {
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();

  const auto& client_id_secret = proto_config.client_credentials().client_id().secret();
  const auto& client_secret_secret = proto_config.client_credentials().client_secret().secret();

  auto secret_provider_client_id_secret =
    secretsProvider(client_id_secret, secret_manager, transport_socket_factory);
  if (secret_provider_client_id_secret == nullptr) {
    throw EnvoyException("invalid oauth2 auth client id secret configuration");
  }

  auto secret_provider_client_secret_secret =
    secretsProvider(client_secret_secret, secret_manager, transport_socket_factory);
  if (secret_provider_client_secret_secret == nullptr) {
    throw EnvoyException("invalid oauth2 auth client secret secret configuration");
  }

  return std::make_unique<Oauth2ClientCredentialsGrantCredentialSource>(cluster_manager, context.threadLocal(), context.api(),
     proto_config,
     secret_provider_client_id_secret, secret_provider_client_secret_secret);
}

/*
 * Static registration for the Credentials Injector filter. @see RegisterFactory.
 */
REGISTER_FACTORY(InjectorConfig, Server::Configuration::NamedHttpFilterConfigFactory);

} // namespace Credentials
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
