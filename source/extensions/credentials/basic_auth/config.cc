#include "source/extensions/credentials/basic_auth/config.h"

#include "envoy/secret/secret_manager.h"
#include "envoy/secret/secret_provider.h"
#include "envoy/upstream/cluster_manager.h"

namespace Envoy {
namespace Extensions {
namespace Credentials {
namespace BasicAuth {

Common::CredentialInjectorSharedPtr
BasicAuthCredentialInjectorFactory::createCredentialInjectorFromProtoTyped(
    const BasicAuth& config, Server::Configuration::FactoryContext& context) {
  const auto& password_secret = config.password();
  auto& cluster_manager = context.clusterManager();
  auto& secret_manager = cluster_manager.clusterManagerFactory().secretManager();
  auto& transport_socket_factory = context.getTransportSocketFactoryContext();
  auto secret_provider_password = Common::secretsProvider(
      password_secret, secret_manager, transport_socket_factory, context.initManager());
  if (secret_provider_password == nullptr) {
    throw EnvoyException("invalid password secret configuration");
  }

  auto secret_reader =
      std::make_shared<Common::SDSSecretReader>(secret_provider_password, context.api());
  return std::make_shared<BasicAuthCredentialInjector>(config.username(), secret_reader);
}

/**
 * Static registration for the basic auth credential injector. @see
 * NamedCredentialInjectorConfigFactory.
 */
REGISTER_FACTORY(BasicAuthCredentialInjectorFactory,
                 Envoy::Extensions::Credentials::Common::NamedCredentialInjectorConfigFactory);

} // namespace BasicAuth
} // namespace Credentials
} // namespace Extensions
} // namespace Envoy
