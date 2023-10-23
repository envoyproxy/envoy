#include "source/extensions/credentials/bearer_token/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BearerToken {

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

Common::CredentialInjectorSharedPtr
BearerTokenCredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const BearerToken& config, Server::Configuration::FactoryContext& context) {
  const std::string& bearer_token_secret = config.bearer_token();
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider_bearer_token = secretsProvider(
      bearer_token_secret, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_bearer_token == nullptr) {
    throw EnvoyException("invalid bearer token secret configuration");
  }

  auto secret_reader =
      std::make_shared<SDSSecretReader>(secret_provider_bearer_token, context.api());
  return std::make_shared<BearerTokenCredentialInjector>(secret_reader);
}

/**
 * Static registration for the bearer token credential injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(BearerTokenCredentialInjectorFactory,
                 Envoy::Extensions::Credentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace BearerToken
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
