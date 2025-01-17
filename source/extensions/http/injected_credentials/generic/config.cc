#include "source/extensions/http/injected_credentials/generic/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace InjectedCredentials {
namespace Generic {

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
GenericCredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const Generic& config, const std::string& /*stats_prefix*/,
    Server::Configuration::FactoryContext& context) {
  const auto& credential_secret = config.credential();
  auto& server_context = context.serverFactoryContext();
  auto& cluster_manager = server_context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider = secretsProvider(credential_secret, secret_manager,
                                         transport_socket_factory, context.initManager());

  auto secret_reader = std::make_shared<const Common::SDSSecretReader>(
      std::move(secret_provider), context.serverFactoryContext().threadLocal(),
      server_context.api());
  std::string header = config.header();
  if (header.empty()) {
    header = "Authorization";
  }
  return std::make_shared<GenericCredentialInjector>(header, secret_reader);
}

/**
 * Static registration for the basic auth credential injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(
    GenericCredentialInjectorFactory,
    Envoy::Extensions::Http::InjectedCredentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace Generic
} // namespace InjectedCredentials
} // namespace Http
} // namespace Extensions
} // namespace Envoy
