#include "source/extensions/http/injected_credentials/oauth2/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace OAuth2 {

namespace {
Secret::GenericSecretConfigProviderSharedPtr
secretsProvider(const envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig& config,
                Secret::SecretManager& secret_manager,
                Server::Configuration::GenericFactoryContext& transport_socket_factory,
                Init::Manager& init_manager) {
  if (config.has_sds_config()) {
    return secret_manager.findOrCreateGenericSecretProvider(config.sds_config(), config.name(),
                                                            transport_socket_factory, init_manager);
  } else {
    return secret_manager.findStaticGenericSecretProvider(config.name());
  }
}
} // namespace

Common::CredentialInjectorSharedPtr
OAuth2CredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const OAuth2& config, const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) {

  switch (config.flow_type_case()) {
  case envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2::FlowTypeCase::
      kClientCredentials:
    return createOauth2ClientCredentialInjector(config, stats_prefix, context, init_manager);
  case envoy::extensions::http::injected_credentials::oauth2::v3::OAuth2::FlowTypeCase::
      FLOW_TYPE_NOT_SET:
    throw EnvoyException("OAuth2 flow type not set");
  }
  return nullptr;
}

Common::CredentialInjectorSharedPtr
OAuth2CredentialInjectorFactory::createOauth2ClientCredentialInjector(
    const OAuth2& proto_config, const std::string& stats_prefix,
    Server::Configuration::ServerFactoryContext& context, Init::Manager& init_manager) {
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getGenericFactoryContext();

  const auto& client_secret_secret = proto_config.client_credentials().client_secret();

  auto client_secret_provider =
      secretsProvider(client_secret_secret, secret_manager, transport_socket_factory, init_manager);
  if (client_secret_provider == nullptr) {
    throw EnvoyException("Invalid oauth2 client secret configuration");
  }

  auto secret_reader = std::make_shared<const Common::SDSSecretReader>(
      std::move(client_secret_provider), context.threadLocal(), context.api());
  auto token_reader = std::make_shared<const TokenProvider>(
      secret_reader, context.threadLocal(), cluster_manager, proto_config,
      context.mainThreadDispatcher(), stats_prefix, context.statsScope());

  return std::make_shared<OAuth2ClientCredentialTokenInjector>(token_reader);
}

/**
 * Static registration for the OAuth2 client credentials injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(
    OAuth2CredentialInjectorFactory,
    Envoy::Extensions::Http::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace OAuth2
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
